// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The 2/4-bit product in integer arithmetic: the inputs to int8 by power-of-2 scales, the tiles (pq_tile16_n), the
// outputs as float, int16 (through an activation table or into the int16 residual) or int8.
#include <math.h>
#include <string.h>
#include "kernels/kernels.h"
#include "kernels/linear.h"
#include "math/fastmath.h"
#include "runtime/parallel.h"

pqi_prof_t pqi_prof;

void pq_tab_build(uint32_t *tab, float (*f)(float), float sy, float sh)
{
    pq_tab_build_k(tab, f, sy, sh, 6);
}

void pq_tab_build_k(uint32_t *tab, float (*f)(float), float sy, float sh, int k)
{
    int32_t prev = 0;
    for (int j = 0; j <= 1 << (16 - k); j++) {
        const float v = f((float)((j << k) - 32768) * sy) / sh;
        const int32_t fj = v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : pq_round(v);
        if (j > 0) {
            int32_t d = fj - prev;
            d = d > 32767 ? 32767 : d < -32768 ? -32768 : d;
            tab[j - 1] = (uint16_t)prev | (uint32_t)(uint16_t)d << 16;
        }
        prev = fj;
    }
}

void pqi_prepare(pqi_t *q, const pq_cqmat_t *w, float ax, float ay, int16_t *n16, uint16_t *tmp)
{
    const int ng = w->in / w->G, n = w->out * ng;
    memcpy(tmp, w->norms, sizeof(uint16_t) * n);
    float maxn = 0.0f;
    for (int i = 0; i < n; i++) {
        maxn = pq_fmax(maxn, pq_f16_to_f32(tmp[i]));
    }
    const float scale = 32767.0f / maxn, sn = maxn / 32767.0f;
    for (int rb = 0; rb < w->out / 16; rb++) {
        for (int g = 0; g < ng; g++) {
            for (int i = 0; i < 16; i++) {
                n16[(rb * ng + g) * 16 + i] = (int16_t)pq_round(pq_f16_to_f32(tmp[(rb * 16 + i) * ng + g]) * scale);
            }
        }
    }
    q->n16 = n16;
    q->inv_sx = 32767.0f / ax;
    const float lsc = w->bits == 2 ? pq_lsc2 : pq_lsc4, target = ay / 32767.0f / 256.0f;
    float U = 64.0f * sn * lsc / (float)w->G * (ax / 32767.0f);
    int S = 0;
    while (U < target) {
        U *= 2.0f;
        S++;
    }
    while (U * 0.5f >= target) {
        U *= 0.5f;
        S--;
    }
    q->S = S;
    q->inv_u = 1.0f / U;
    q->sy = 256.0f * U;
    q->n = 8;
}

int pqi_prepare_res(pqi_t *q, const pq_cqmat_t *w, float ax, float ay, float s_res, int16_t *n16, uint16_t *tmp)
{
    pqi_prepare(q, w, ax, ay, n16, tmp);   // (tmp keeps the float16 norms)
    const float U = q->sy / 256.0f;
    int n = 0;
    while (n < 30 && U * (float)(1 << (n + 1)) <= s_res) {
        n++;
    }
    const float c = s_res / (U * (float)(1 << n));
    if (n < 1 || c < 1.0f || c >= 2.0f) {
        return -1;
    }
    const int ng = w->in / w->G, nn = w->out * ng;
    float maxn = 0.0f;
    for (int i = 0; i < nn; i++) {
        maxn = pq_fmax(maxn, pq_f16_to_f32(tmp[i]));
    }
    const float scale = 32767.0f / maxn / c;
    for (int rb = 0; rb < w->out / 16; rb++) {
        for (int g = 0; g < ng; g++) {
            for (int i = 0; i < 16; i++) {
                n16[(rb * ng + g) * 16 + i] = (int16_t)pq_round(pq_f16_to_f32(tmp[(rb * 16 + i) * ng + g]) * scale);
            }
        }
    }
    q->inv_u = 1.0f / (U * c);
    q->sy = s_res;
    q->n = n;
    return n;
}

size_t pq_linear_int_scratch(const pq_cqmat_t *w, int T)
{
    return (size_t)T * w->in + 32 + 16 + (size_t)T * (w->in / w->G) + 16;
}

typedef struct {
    const pq_cqmat_t *w;
    const pqi_t *q;
    const uint16_t *bias;
    const float *x;
    const int16_t *x16;
    float *y;
    int16_t *y16;
    const uint32_t *tab;
    int T, ldx, ldy, post, res_j, tab_row0, tab_row1, sh8;
    int8_t *y8;
    const int16_t *N8;
    int8_t *xq;
    uint8_t *sar;          // [ng][T]: max(0, S - e), at most 31
    pq_bufs_t bufs;
} ci_job_t;

// float -> integer (round to nearest even; not clamped to int16: the Hadamard is int32, |x * inv| stays far below
// 2^24 in practice, and fcvt saturates beyond 2^31), then 3 butterfly stages (1, 2, 4) in registers. The 8
// conversions are independent, so their float latencies overlap on the single-issue core.
static inline void conv_fwht8(const float *x, float inv, int32_t *v)
{
    const float f0 = x[0] * inv, f1 = x[1] * inv, f2 = x[2] * inv, f3 = x[3] * inv;
    const float f4 = x[4] * inv, f5 = x[5] * inv, f6 = x[6] * inv, f7 = x[7] * inv;
    const int32_t a0 = pq_round(f0), a1 = pq_round(f1), a2 = pq_round(f2), a3 = pq_round(f3);
    const int32_t a4 = pq_round(f4), a5 = pq_round(f5), a6 = pq_round(f6), a7 = pq_round(f7);
    const int32_t b0 = a0 + a1, b1 = a0 - a1, b2 = a2 + a3, b3 = a2 - a3;
    const int32_t b4 = a4 + a5, b5 = a4 - a5, b6 = a6 + a7, b7 = a6 - a7;
    const int32_t c0 = b0 + b2, c2 = b0 - b2, c1 = b1 + b3, c3 = b1 - b3;
    const int32_t c4 = b4 + b6, c6 = b4 - b6, c5 = b5 + b7, c7 = b5 - b7;
    v[0] = c0 + c4;
    v[4] = c0 - c4;
    v[1] = c1 + c5;
    v[5] = c1 - c5;
    v[2] = c2 + c6;
    v[6] = c2 - c6;
    v[3] = c3 + c7;
    v[7] = c3 - c7;
}

// float -> integer only (8 independent conversions)
static inline void conv8(const float *x, float inv, int32_t *v)
{
    const float f0 = x[0] * inv, f1 = x[1] * inv, f2 = x[2] * inv, f3 = x[3] * inv;
    const float f4 = x[4] * inv, f5 = x[5] * inv, f6 = x[6] * inv, f7 = x[7] * inv;
    v[0] = pq_round(f0);
    v[1] = pq_round(f1);
    v[2] = pq_round(f2);
    v[3] = pq_round(f3);
    v[4] = pq_round(f4);
    v[5] = pq_round(f5);
    v[6] = pq_round(f6);
    v[7] = pq_round(f7);
}

// phase 1: int16 input, Hadamard, power-of-2 scale, int8 (frames of this part)
static void ci_quant_part(void *ctx, int part, int parts)
{
    const ci_job_t *j = ctx;
    const int G = j->w->G, in = j->w->in, ng = in / G, T = j->T, S = j->q->S;
    const float inv = j->q->inv_sx;
    int t0, t1;
    pq_par_range(T, part, parts, 1, &t0, &t1);
    int32_t r[128] __attribute__((aligned(16)));
    uint32_t left = 0, cf = 0, cm = 0, cr = 0;
    for (int t = t0; t < t1; t++) {
        for (int g = 0; g < ng; g++) {
            const uint32_t c0 = pq_cycles();
            uint32_t m;   // bitlen(max |r|) (= bitlen(OR of |r|))
            if (j->x16) {
                m = pq_fwht_i16(j->x16 + (size_t)t * j->ldx + g * G, r, G);
            } else {
                const float *xs = j->x + (size_t)t * j->ldx + g * G;
#ifdef PQ_PIE
                for (int k = 0; k < G; k += 8) {   // the conversion here, the Hadamard on the PIE kernel
                    conv8(xs + k, inv, r + k);
                }
                m = pq_fwht_i32(r, G);
#else
                for (int k = 0; k < G; k += 8) {
                    conv_fwht8(xs + k, inv, r + k);
                }
                m = pq_fwht_i32_from8(r, G);
#endif
            }
            const uint32_t c1 = pq_cycles(), c2 = c1;
            int e = m ? 32 - __builtin_clz(m) - 7 : 0;
            e = e < 0 ? 0 : e;
            pq_q8_shift(r, G, e, j->xq + (size_t)t * in + g * G);
            const uint32_t c3 = pq_cycles();
            cf += c1 - c0;
            cm += c2 - c1;
            cr += c3 - c2;
            int s = S - e;
            left += s < 0;
            s = s < 0 ? 0 : s > 31 ? 31 : s;
            j->sar[g * T + t] = (uint8_t)s;
        }
    }
    if (part == 0) {
        pqi_prof.q_fwht += cf;
        pqi_prof.q_max += cm;
        pqi_prof.q_round += cr;
        pqi_prof.left += left;
        pqi_prof.groups += (uint64_t)(t1 - t0) * ng;
    }
}

// phase 2: the tiles of 16 rows of this part
static void ci_tiles_part(void *ctx, int part, int parts)
{
    const ci_job_t *j = ctx;
    const pq_cqmat_t *w = j->w;
    const int G = w->G, in = w->in, ng = in / G, tile = G * 2 * w->bits, T = j->T;
    const float inv_u = j->q->inv_u, sy = j->q->sy;
    const int n = j->q->n;
    int32_t *y32 = j->bufs.acc[part];
    int8_t *wdec = j->bufs.wdec[part];
    const uint32_t *lut = pq_lut(w->bits);
    int rb0, rb1;
    pq_par_range(w->out / 16, part, parts, 1, &rb0, &rb1);
    uint32_t cd = 0, ct = 0, crest = 0;
    for (int rb = rb0; rb < rb1; rb++) {
        const uint32_t c0 = pq_cycles();
        int32_t b[16];
        for (int i = 0; i < 16; i++) {
            b[i] = (1 << (n - 1)) + (j->bias ? pq_round(pq_f16_to_f32(j->bias[rb * 16 + i]) * inv_u) : 0);
        }
        for (int t = 0; t < T; t++) {
            memcpy(y32 + t * 16, b, sizeof(b));
        }
        uint32_t c1 = pq_cycles();
        crest += c1 - c0;
        for (int g = 0; g < ng; g++) {
            const uint8_t *codes = w->codes + (size_t)(rb * ng + g) * tile;
            if (w->bits == 2) {
                pq_decode2(codes, tile, lut, wdec);
            } else {
                pq_decode4(codes, tile, lut, wdec);
            }
            const uint32_t c2 = pq_cycles();
            pq_tile16_n(wdec, j->xq + g * G, in, T, G, j->q->n16 + (size_t)(rb * ng + g) * 16, j->sar + g * T, y32);
            const uint32_t c3 = pq_cycles();
            cd += c2 - c1;
            ct += c3 - c2;
            c1 = c3;
        }
        const uint32_t c4 = pq_cycles();
        if (j->y8) {
            static const int16_t one16[16] __attribute__((aligned(16))) = { 1, 1, 1, 1, 1, 1, 1, 1,
                                                                            1, 1, 1, 1, 1, 1, 1, 1 };
            static const int32_t zero32[16] __attribute__((aligned(16)));
            pq_out_act8(y32, T, one16, zero32, j->N8 + rb * 16, n, 0, j->sh8, NULL, j->y8 + rb * 16, j->ldy);
        } else if (j->y16 && j->post == PQI_POST_ADD16) {
            pq_out_add16(y32, T, n, j->res_j, j->y16 + rb * 16, j->ldy);
        } else if (j->y16) {
            const int in_tab = rb * 16 >= j->tab_row0 && (!j->tab_row1 || rb * 16 < j->tab_row1);
            pq_out_tab16(y32, T, in_tab ? j->tab : NULL, j->y16 + rb * 16, j->ldy);
        }
        // float output (not clamped to int16 yet); one loop per post
        for (int t = 0; t < T && !j->y16 && !j->y8; t++) {
            float *yt = j->y + (size_t)t * j->ldy + rb * 16;
            const int32_t *a = y32 + t * 16;
            if (j->post == PQI_POST_ADD) {
#pragma GCC unroll 16
                for (int i = 0; i < 16; i++) {
                    yt[i] = yt[i] + (float)(a[i] >> n) * sy;
                }
            } else if (j->post == PQI_POST_SWOOSH_L) {
#pragma GCC unroll 16
                for (int i = 0; i < 16; i++) {
                    yt[i] = pq_fm_swoosh_l_tab((float)(a[i] >> n) * sy);
                }
            } else {
#pragma GCC unroll 16
                for (int i = 0; i < 16; i++) {
                    yt[i] = (float)(a[i] >> n) * sy;
                }
            }
        }
        crest += pq_cycles() - c4;
    }
    if (part == 0) {
        pqi_prof.t_decode += cd;
        pqi_prof.t_tile += ct;
        pqi_prof.t_rest += crest;
    }
}

void pq_linear_int(const pq_cqmat_t *w, const pqi_t *q, const uint16_t *bias, const float *x, int T, int ldx, float *y,
                   int ldy, void *scratch, int post)
{
    const pqi_io_t io = { .x = x, .T = T, .ldx = ldx, .y = y, .ldy = ldy, .post = post, .scratch = scratch };
    pq_linear_int_io(w, q, bias, &io);
}

void pq_linear_int_io(const pq_cqmat_t *w, const pqi_t *q, const uint16_t *bias, const pqi_io_t *io)
{
    const int in = w->in, ng = in / w->G, T = io->T;
    void *scratch = io->scratch;
    ci_job_t j = { .w = w, .q = q, .bias = bias, .x = io->x, .x16 = io->x16, .y = io->y, .y16 = io->y16,
                   .tab = io->tab, .T = T, .ldx = io->ldx, .ldy = io->ldy, .post = io->post,
                   .res_j = io->res_j, .tab_row0 = io->tab_row0, .tab_row1 = io->tab_row1, .y8 = io->y8,
                   .N8 = io->N8, .sh8 = io->sh8 };
    pq_get_bufs(&j.bufs);
    uint8_t *p = (uint8_t *)(((uintptr_t)scratch + 15) & ~(uintptr_t)15);
    j.xq = (size_t)T * in + 32 <= j.bufs.xq_cap ? j.bufs.xq : (int8_t *)p;
    p += ((size_t)T * in + 32 + 15) / 16 * 16;
    j.sar = (size_t)T * ng <= j.bufs.sx_cap ? (uint8_t *)j.bufs.sx : p;
    uint32_t c0 = pq_cycles();
    pq_par_run(ci_quant_part, &j);
    uint32_t c1 = pq_cycles();
    pq_par_run(ci_tiles_part, &j);
    pqi_prof.quant += c1 - c0;
    pqi_prof.tiles += pq_cycles() - c1;
}
