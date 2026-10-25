// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The conv module: in_proj, GLU, the causal and chunk depthwise convolutions with their cache, SwooshR, out_proj.
#include <math.h>
#include <string.h>
#include "encoder/encoder_internal.h"
#include "math/fastmath.h"

typedef struct {
    const pq_stack_t *st;
    const pq_layer_t *ly;
    int which;
    const float *h;
    float *xc, *yt;
    const int16_t *h16;    // PIEASR_I16_CV: cv_in's int16 output [S][2D], and the int16 output yt16 [S][D]
    int16_t *yt16;
} convdw_job_t;
static void convdw_part(void *ctx, int part, int parts);

void pq_conv_module(pieasr_model_t *m, const pq_stack_t *st, pq_layer_t *ly, int which, const float *x, float *y,
                    int acc)
{
    const int D = st->D, S = st->S, K = st->K, lp = K / 2;
    size_t mark = m->ws_used;
    if (pieasr_opt_int && ly->cv_cc[which]) {   // PIEASR_I16_CV
        const pq_lin_t *li = &ly->cv_in[which], *lo = &ly->cv_out[which];
        int16_t *h16 =
            (int16_t *)(((uintptr_t)pq_ws_alloc(m, ((size_t)S * 2 * D + 8) / 2 + 4) + 15) & ~(uintptr_t)15);
        int16_t *yt16 = (int16_t *)(((uintptr_t)pq_ws_alloc(m, ((size_t)S * D + 8) / 2 + 4) + 15) & ~(uintptr_t)15);
        PQ_PROF_START();
        size_t mk = m->ws_used;
        pqi_io_t a = { .T = S, .ldx = li->w.in, .y16 = h16, .tab = ly->cv_tabb[which], .tab_row0 = D, .ldy = 2 * D,
                       .scratch = pq_ws_alloc(m, (pq_linear_int_scratch(&li->w, S) + 3) / 4) };
        pq_io_in(m, &a, x);
        pq_linear_int_io(&li->w, &li->qi, li->b, &a);
        m->ws_used = mk;
        PQ_PROF_END(linear);
        prof_t0 = pieasr_clock ? pieasr_clock() : 0;
        convdw_job_t job = { .st = st, .ly = ly, .which = which, .h16 = h16, .yt16 = yt16 };
        pq_par_run(convdw_part, &job);
        PQ_PROF_END(conv_dw);
        prof_t0 = pieasr_clock ? pieasr_clock() : 0;
        pqi_io_t b = { .x16 = yt16, .T = S, .ldx = D, .ldy = lo->w.out,
                       .scratch = pq_ws_alloc(m, (pq_linear_int_scratch(&lo->w, S) + 3) / 4) };
        pq_io_out(m, &b, y, acc ? PQI_POST_ADD : PQI_POST_NONE);
        pq_linear_int_io(&lo->w, &lo->qi, lo->b, &b);
        PQ_PROF_END(linear);
        m->ws_used = mark;
        return;
    }
    float *h = pq_ws_alloc(m, (size_t)S * 2 * D);
    pq_linear(m, &ly->cv_in[which], x, S, h);
    float *xc = pq_ws_alloc(m, (size_t)D * (lp + S));
    float *yt = pq_ws_alloc(m, (size_t)S * D);
    PQ_PROF_START();
    convdw_job_t job = { .st = st, .ly = ly, .which = which, .h = h, .xc = xc, .yt = yt };
    pq_par_run(convdw_part, &job);
    PQ_PROF_END(conv_dw);
    pq_linear_p(m, &ly->cv_out[which], yt, S, y, acc ? PQI_POST_ADD : PQI_POST_NONE);
    m->ws_used = mark;
}

// the conv module's GLU, cache update and depthwise convs, channels split between the cores in blocks of 16:
// xc [16, lp + S] = concat(cache, GLU^T) in internal RAM; y [S, D] (transposed back), swoosh_r applied, written
// 16 channels (one 64-byte line) per frame
static float cv_xc[PQ_PAR_PARTS][16 * (15 + 64)];
static float cv_y[PQ_PAR_PARTS][64 * 16];

pq_dw_buf_t pq_dwq[PQ_PAR_PARTS];
int32_t pq_dw_off[32];

// the int8 version of one 16-channel block (pieasr_opt_q8 == 2): the GLU outputs and the cache quantized with one
// scale per channel (max over cache and chunk), the taps int8 per channel (pq_prep_conv_taps); pq_dw16 for the causal
// taps over [cache | chunk] and for the chunk taps over [zeros | chunk | zeros]; then the same float combination.
static void convdw_block_q8(const convdw_job_t *jb, int db, int part, float *y16)
{
    const uint32_t c0 = pq_cycles();
    const pq_stack_t *st = jb->st;
    const pq_layer_t *ly = jb->ly;
    const int which = jb->which, D = st->D, S = st->S, K = st->K, lp = K / 2;
    const pq_dw_buf_t *b = &pq_dwq[part];
    float *cache = ly->conv[which];
    float *gb = b->gb;
    for (int k = 0; k < lp; k++) {
        for (int i = 0; i < 16; i++) {
            gb[k * 16 + i] = cache[(size_t)(db + i) * lp + k];
        }
    }
    for (int t = 0; t < S; t++) {
        const float *r = jb->h + (size_t)t * 2 * D + db;
        for (int i = 0; i < 16; i++) {
            gb[(lp + t) * 16 + i] = r[i] * pq_fm_sigmoid_tab(r[D + i]);
        }
    }
    const uint32_t c1 = pq_cycles();
    for (int k = 0; k < lp; k++) {   // the new cache: the last lp of [cache | chunk]
        for (int i = 0; i < 16; i++) {
            cache[(size_t)(db + i) * lp + k] = gb[(S + k) * 16 + i];
        }
    }
    float mx[16] = { 0 }, sx[16], inv[16];
    for (int r = 0; r < lp + S; r++) {
        for (int i = 0; i < 16; i++) {
            mx[i] = pq_fmax(mx[i], fabsf(gb[r * 16 + i]));
        }
    }
    for (int i = 0; i < 16; i++) {
        sx[i] = mx[i] / 127.0f;
        inv[i] = mx[i] > 0.0f ? 127.0f / mx[i] : 0.0f;
    }
    memset(b->gz, 0, (size_t)lp * 16);
    for (int r = 0; r < lp + S; r++) {
        for (int i = 0; i < 16; i++) {
            const int8_t q = (int8_t)pq_round(gb[r * 16 + i] * inv[i]);
            b->xc[r * 16 + i] = q;
            if (r >= lp) {
                b->gz[r * 16 + i] = q;
            }
        }
    }
    memset(b->gz + (lp + S) * 16, 0, (size_t)lp * 16);
    const uint32_t c2 = pq_cycles();
    pq_dw16(b->xc, 16, S, pq_dw_off, ly->cv_wc8[which] + (size_t)(db / 16) * (lp + 1) * 16, lp + 1, b->accc);
    pq_dw16(b->gz, 16, S, pq_dw_off, ly->cv_wk8[which] + (size_t)(db / 16) * K * 16, K, b->acck);
    const uint32_t c3 = pq_cycles();
    for (int i = 0; i < 16; i++) {
        const int d = db + i;
        const float scc = ly->cv_swc[which][d] * sx[i], sck = ly->cv_swk[which][d] * sx[i];
        const float bcf = ly->cv_bcf[which][d], bkf = ly->cv_bkf[which][d];
        const float *es = ly->cv_es[which] + (size_t)d * S;   // 1 + (left + right), precomputed
        for (int t = 0; t < S; t++) {
            const float yc = scc * (float)b->accc[t * 16 + i] + bcf;
            const float yk = sck * (float)b->acck[t * 16 + i] + bkf;
            y16[t * 16 + i] = pq_fm_swoosh_r_tab(yk * es[t] + yc);
        }
    }
    if (part == 0) {
        pieasr_prof.dw_glu += c1 - c0;
        pieasr_prof.dw_q += c2 - c1;
        pieasr_prof.dw_k += c3 - c2;
        pieasr_prof.dw_comb += pq_cycles() - c3;
    }
}

// PIEASR_I16_CV: one 16-channel block with dynamic scales. g16 [lp + S][16] = [int16 cache | GLU (pq_mul16)]; per
// channel e = max(0, bitlen(max |g16|) - 7) -> int8 = round(g16 / 2^e) (pq_lane_q8); the combine's multipliers for
// these scales (the step of channel i: cv_in's step * 2^e_i), with the shift sh chosen per block
static void convdw_block_dyn(const convdw_job_t *jb, int db, int part)
{
    const uint32_t c0 = pq_cycles();
    const pq_stack_t *st = jb->st;
    const pq_layer_t *ly = jb->ly;
    const int w = jb->which, D = st->D, S = st->S, K = st->K, lp = K / 2, r = ly->cv_r[w];
    const pq_dw_buf_t *b = &pq_dwq[part];
    int16_t *g16 = b->g16;
    int16_t *cache = (int16_t *)ly->conv[w] + (size_t)(db / 16) * lp * 16;
    memcpy(g16, cache, sizeof(int16_t) * lp * 16);
    pq_mul16(jb->h16 + db, jb->h16 + D + db, 2 * D, S, g16 + lp * 16);
    memcpy(cache, g16 + (size_t)S * 16, sizeof(int16_t) * lp * 16);
    int32_t mx[16];
    pq_lane_or16(g16, lp + S, mx);
    int16_t N[16] __attribute__((aligned(16)));
    int e[16];
    for (int i = 0; i < 16; i++) {
        const int bl = mx[i] ? 32 - __builtin_clz((uint32_t)mx[i]) : 0;
        e[i] = bl > 7 ? bl - 7 : 0;
        N[i] = (int16_t)(e[i] ? 1 << (15 - e[i]) : 32767);   // (e = 0: 32767 rounds every |g| <= 127 to itself)
    }
    pq_lane_q8(g16, lp + S, N, b->xc, 16);
    memset(b->gz, 0, (size_t)lp * 16);
    memcpy(b->gz + lp * 16, b->xc + lp * 16, (size_t)S * 16);
    memset(b->gz + (lp + S) * 16, 0, (size_t)lp * 16);
    // the combine's multipliers: channel d's int8 step = sy 2^e (cv_cc: the per-channel factors, from init)
    const float *cc = ly->cv_cc[w];
    float gmax = 0.0f, fc[16], fk[16];
    for (int i = 0; i < 16; i++) {
        const float p2 = (float)(1 << e[i]);
        fc[i] = cc[db + i] * p2;
        fk[i] = cc[D + db + i] * p2;
        gmax = pq_fmax(gmax, pq_fmax(fabsf(fc[i]), fabsf(fk[i])));
    }
    int sh = 1;
    while (sh < 30 && gmax * (float)(1 << (sh + 1)) <= 32767.0f) {
        sh++;
    }
    pq_comb_t *P = b->comb;
    const float p2s = (float)(1 << sh);
    for (int i = 0; i < 16; i++) {
        P->Mc[i] = (int16_t)pq_round(fc[i] * p2s);
        P->Mk[i] = (int16_t)pq_round(fk[i] * p2s);
        P->Bc[i] = pq_round(cc[2 * D + db + i] * p2s) + (1 << (sh - 1));
        P->Bk[i] = pq_round(cc[3 * D + db + i] * p2s) + (1 << (sh - 1));
    }
    const uint32_t c1 = pq_cycles();
    pq_dw16(b->xc, 16, S, pq_dw_off, ly->cv_wc8[w] + (size_t)(db / 16) * (lp + 1) * 16, lp + 1, b->accc);
    pq_dw16(b->gz, 16, S, pq_dw_off, ly->cv_wk8[w] + (size_t)(db / 16) * K * 16, K, b->acck);
    const uint32_t c2 = pq_cycles();
    pq_dwcomb16(b->accc, b->acck, S, P, r, sh, ly->cv_es16[w] + (size_t)(db / 16) * S * 16, ly->cv_tabr[w],
                jb->yt16 + db, D);
    if (part == 0) {
        pieasr_prof.dw_glu += c1 - c0;
        pieasr_prof.dw_k += c2 - c1;
        pieasr_prof.dw_comb += pq_cycles() - c2;
    }
}

static void convdw_part(void *ctx, int part, int parts)
{
    if (((const convdw_job_t *)ctx)->h16) {
        const convdw_job_t *jb = ctx;
        int d0, d1;
        pq_par_range(jb->st->D, part, parts, 16, &d0, &d1);
        for (int db = d0; db < d1; db += 16) {
            convdw_block_dyn(jb, db, part);
        }
        return;
    }
    if (pieasr_opt_q8 == 2 && pq_dwq[part].xc) {
        const convdw_job_t *jb = ctx;
        const int D = jb->st->D, S = jb->st->S;
        float *y16 = cv_y[part];
        int d0, d1;
        pq_par_range(D, part, parts, 16, &d0, &d1);
        for (int db = d0; db < d1; db += 16) {
            convdw_block_q8(jb, db, part, y16);
            const uint32_t c0 = pq_cycles();
            for (int t = 0; t < S; t++) {
                memcpy(jb->yt + (size_t)t * D + db, y16 + t * 16, sizeof(float) * 16);
            }
            if (part == 0) {
                pieasr_prof.dw_out += pq_cycles() - c0;
            }
        }
        return;
    }
    const convdw_job_t *jb = ctx;
    const pq_stack_t *st = jb->st;
    const pq_layer_t *ly = jb->ly;
    const int which = jb->which, D = st->D, S = st->S, K = st->K, lp = K / 2, w = lp + S;
    const float *h = jb->h;
    float *yt = jb->yt, *xc = cv_xc[part], *y16 = cv_y[part];
    float *cache = ly->conv[which];
    const uint16_t *wc = ly->cv_causal_w[which], *bc = ly->cv_causal_b[which];
    const uint16_t *wk = ly->cv_chunk_w[which], *bk = ly->cv_chunk_b[which], *es = ly->cv_scale[which];
    int d0, d1;
    pq_par_range(D, part, parts, 16, &d0, &d1);
    for (int db = d0; db < d1; db += 16) {
        for (int i = 0; i < 16; i++) {
            memcpy(xc + i * w, cache + (size_t)(db + i) * lp, sizeof(float) * lp);
        }
        for (int t = 0; t < S; t++) {
            const float *r = h + (size_t)t * 2 * D + db;
            for (int i = 0; i < 16; i++) {
                xc[i * w + lp + t] = r[i] * pq_fm_sigmoid_tab(r[D + i]);
            }
        }
        for (int i = 0; i < 16; i++) {
            memcpy(cache + (size_t)(db + i) * lp, xc + i * w + S, sizeof(float) * lp);
        }
        for (int i = 0; i < 16; i++) {
            const int d = db + i;
            const float *row = xc + i * w;
            const float *g = row + lp;   // the chunk's GLU outputs
            float wcf[16], wkf[31];      // this channel's taps as floats (K <= 31)
            for (int k = 0; k <= lp; k++) {
                wcf[k] = pq_h2f(wc, d * (lp + 1) + k);
            }
            for (int k = 0; k < K; k++) {
                wkf[k] = pq_h2f(wk, d * K + k);
            }
            const float bcf = pq_h2f(bc, d), bkf = pq_h2f(bk, d);
            // tap-major loops: the S outputs are independent (no add chain), each still summed over k in order
            float yca[64], yka[64];
            for (int t = 0; t < S; t++) {
                yca[t] = 0.0f;
                yka[t] = 0.0f;
            }
            for (int k = 0; k <= lp; k++) {
                const float wv = wcf[k];
                const float *r = row + k;
                for (int t = 0; t < S; t++) {
                    yca[t] += wv * r[t];
                }
            }
            for (int k = 0; k < K; k++) {   // chunk taps: output t uses g[t + k - lp] when it is inside the chunk
                const float wv = wkf[k];
                const int t0 = k < lp ? lp - k : 0, t1 = S + lp - k < S ? S + lp - k : S;
                const float *r = g + k - lp;
                for (int t = t0; t < t1; t++) {
                    yka[t] += wv * r[t];
                }
            }
            for (int t = 0; t < S; t++) {
                const float yc = yca[t] + bcf;
                const float yk = yka[t] + bkf;
                float left, right;
                if (S < K) {
                    left = pq_h2f(es, d * K + t);
                    right = pq_h2f(es, D * K + d * K + (K - S) + t);
                } else {
                    left = t < K ? pq_h2f(es, d * K + t) : 0.0f;
                    right = t >= S - K ? pq_h2f(es, D * K + d * K + t - (S - K)) : 0.0f;
                }
                float v = yk * (1.0f + (left + right)) + yc;
                y16[t * 16 + i] = pq_fm_swoosh_r_tab(v);
            }
        }
        for (int t = 0; t < S; t++) {
            memcpy(yt + (size_t)t * D + db, y16 + t * 16, sizeof(float) * 16);
        }
    }
}
