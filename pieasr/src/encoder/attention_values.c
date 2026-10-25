// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The attention weights applied to the values: the self-attentions and the NonlinAttention.
#include <math.h>
#include <string.h>
#include "encoder/encoder_internal.h"
#include "math/fastmath.h"

// per-core internal-RAM buffer of the attn@V loop: accumulators [S][w] (w <= 72: S*w <= 4608)
static float att_o[PQ_PAR_PARTS][64 * 72 + 64];

// o [S][heads * w] = per head h: attn[h] [S][n] @ V[:, h*w : (h+1)*w] (V [n][ldv]); optional gate: o *= gate
// [S][ldg]. Value-major loop: each V row is read once; the sums run over j in order, as the reference loop.
// Several heads: heads split between the cores; one head (the nonlin attention): its columns split.
typedef struct {
    const float *attn, *V, *gate;
    const int16_t *gate16; // or the gate as int16 (value gate16 * gs)
    float gs;
    const int8_t *V8;      // pieasr_opt_int: int8 values [n][ldv] with a static scale per column vs [ldv] (V unused)
    const float *vs;
    float *o;
    int S, n, w, ldv, ldo, ldg, heads, hstride_attn;
} av_job_t;

static void av_cols(const av_job_t *jb, int h, int c0, int c1, float *acc)
{
    const int S = jb->S, n = jb->n, cw = c1 - c0;
    const float *A = jb->attn + (size_t)h * jb->hstride_attn;
    memset(acc, 0, sizeof(float) * S * cw);
    for (int j = 0; j < n; j++) {
        const float *v = jb->V + (size_t)j * jb->ldv + h * jb->w + c0;
        for (int t = 0; t < S; t++) {
            const float a = A[(size_t)t * n + j];
            float *o = acc + t * cw;
            for (int c = 0; c < cw; c++) {
                o[c] += a * v[c];
            }
        }
    }
    for (int t = 0; t < S; t++) {
        float *o = jb->o + (size_t)t * jb->ldo + h * jb->w + c0;
        const float *g = jb->gate ? jb->gate + (size_t)t * jb->ldg + c0 : NULL;
        for (int c = 0; c < cw; c++) {
            o[c] = g ? acc[t * cw + c] * g[c] : acc[t * cw + c];
        }
    }
}

// The same product on the tile kernel (pieasr_opt_q8 == 2): the attention rows are int8 with one scale per row
// (max / 127), the values int8 with one scale per column; per 16 columns, a tile [n][16] of V and pq_tile16.
static void av_cols_q8(const av_job_t *jb, int h, int c0, int c1, const pq_av_buf_t *b)
{
    const int S = jb->S, n = jb->n, np = (n + 15) & ~15;
    const float *A = jb->attn + (size_t)h * jb->hstride_attn;
    for (int t = 0; t < S; t++) {
        const float *a = A + (size_t)t * n;
        float amax = 0.0f;
        for (int j = 0; j < n; j++) {
            amax = pq_fmax(amax, a[j]);   // softmax outputs: >= 0
        }
        const float inv = amax > 0.0f ? 127.0f / amax : 0.0f;
        b->xs[t] = amax / 127.0f;
        int8_t *q = b->xq + (size_t)t * np;
        for (int j = 0; j < n; j++) {
            q[j] = (int8_t)pq_round(a[j] * inv);
        }
        for (int j = n; j < np; j++) {
            q[j] = 0;
        }
    }
    for (int cb = c0; cb < c1; cb += 16) {
        const int cw = c1 - cb < 16 ? c1 - cb : 16;
        const float *V = jb->V + h * jb->w + cb;
        float vmax[16] = { 0 }, vs[16], vinv[16];
        for (int j = 0; j < n; j++) {
            const float *v = V + (size_t)j * jb->ldv;
            for (int i = 0; i < cw; i++) {
                const float a = fabsf(v[i]);
                vmax[i] = pq_fmax(vmax[i], a);
            }
        }
        for (int i = 0; i < 16; i++) {
            vs[i] = i < cw ? vmax[i] / 127.0f : 0.0f;
            vinv[i] = i < cw && vmax[i] > 0.0f ? 127.0f / vmax[i] : 0.0f;
        }
        for (int j = 0; j < n; j++) {
            const float *v = V + (size_t)j * jb->ldv;
            int8_t *vt = b->vt + j * 16;
            for (int i = 0; i < 16; i++) {
                vt[i] = i < cw ? (int8_t)pq_round(v[i] * vinv[i]) : 0;
            }
        }
        memset(b->vt + n * 16, 0, (size_t)(np - n) * 16);
        pq_tile16(b->vt, b->xq, np, S, np, b->acc);
        for (int t = 0; t < S; t++) {
            float *o = jb->o + (size_t)t * jb->ldo + h * jb->w + cb;
            const float *g = jb->gate ? jb->gate + (size_t)t * jb->ldg + cb : NULL;
            for (int i = 0; i < cw; i++) {
                const float v = (vs[i] * b->xs[t]) * (float)b->acc[t * 16 + i];
                o[i] = g ? v * g[i] : v;
            }
        }
    }
}

// attn@V with the int8 value cache (pieasr_opt_int): as av_cols_q8, with the values' static per-column scales instead
// of a per-call max over the rows: the tiles are copies of the cache rows
static void av_cols_q8i(const av_job_t *jb, int h, int c0, int c1, const pq_av_buf_t *b)
{
    const int S = jb->S, n = jb->n, np = (n + 15) & ~15;
    const float *A = jb->attn + (size_t)h * jb->hstride_attn;
    const int8_t *xq = b->xq;
    const float *xs = b->xs;
    if (pq_att8) {   // the softmax rows already int8 (scores_part): no float pass here
        xq = pq_att8 + (size_t)(jb->hstride_attn ? h : 0) * S * pq_att8_np;
        xs = pq_att8_s + (jb->hstride_attn ? h : 0) * S;
    }
    for (int t = 0; t < S && !pq_att8; t++) {
        const float *a = A + (size_t)t * n;
        float amax = 0.0f;
        for (int j = 0; j < n; j++) {
            amax = pq_fmax(amax, a[j]);   // softmax outputs: >= 0
        }
        const float inv = amax > 0.0f ? 127.0f / amax : 0.0f;
        b->xs[t] = amax / 127.0f;
        int8_t *q = b->xq + (size_t)t * np;
        for (int j = 0; j < n; j++) {
            q[j] = (int8_t)pq_round(a[j] * inv);
        }
        for (int j = n; j < np; j++) {
            q[j] = 0;
        }
    }
    for (int cb = c0; cb < c1; cb += 16) {
        const int cw = c1 - cb < 16 ? c1 - cb : 16, col = h * jb->w + cb;
        float vs[16];
        for (int i = 0; i < 16; i++) {
            vs[i] = i < cw ? jb->vs[col + i] : 0.0f;
        }
        for (int j = 0; j < n; j++) {
            int8_t *vt = b->vt + j * 16;
            memcpy(vt, jb->V8 + (size_t)j * jb->ldv + col, cw);
            if (cw < 16) {
                memset(vt + cw, 0, 16 - cw);
            }
        }
        memset(b->vt + n * 16, 0, (size_t)(np - n) * 16);
        pq_tile16(b->vt, xq, np, S, np, b->acc);
        for (int t = 0; t < S; t++) {
            float *o = jb->o + (size_t)t * jb->ldo + h * jb->w + cb;
            const float *g = jb->gate ? jb->gate + (size_t)t * jb->ldg + cb : NULL;
            const int16_t *g16 = jb->gate16 ? jb->gate16 + (size_t)t * jb->ldg + cb : NULL;
            for (int i = 0; i < cw; i++) {
                const float v = (vs[i] * xs[t]) * (float)b->acc[t * 16 + i];
                o[i] = g16 ? v * ((float)g16[i] * jb->gs) : g ? v * g[i] : v;
            }
        }
    }
}

static void av_part(void *ctx, int part, int parts)
{
    const av_job_t *jb = ctx;
    float *acc = att_o[part];
    const int q8 = pieasr_opt_q8 == 2 && pq_avq[part].xq;
    if (jb->heads >= parts) {
        int h0, h1;
        pq_par_range(jb->heads, part, parts, 1, &h0, &h1);
        for (int h = h0; h < h1; h++) {
            if (q8) {
                if (jb->V8) {
                    av_cols_q8i(jb, h, 0, jb->w, &pq_avq[part]);
                } else {
                    av_cols_q8(jb, h, 0, jb->w, &pq_avq[part]);
                }
            } else {
                av_cols(jb, h, 0, jb->w, acc);
            }
        }
    } else {
        int c0, c1;
        pq_par_range(jb->w, part, parts, q8 ? 16 : 4, &c0, &c1);
        if (q8) {
            if (c0 < c1) {
                if (jb->V8) {
                    av_cols_q8i(jb, 0, c0, c1, &pq_avq[part]);
                } else {
                    av_cols_q8(jb, 0, c0, c1, &pq_avq[part]);
                }
            }
            return;
        }
        // in pieces of at most 4608 / S columns (the accumulator size)
        const int step = 4608 / jb->S;
        for (int c = c0; c < c1; c += step) {
            av_cols(jb, 0, c, c + step < c1 ? c + step : c1, acc);
        }
    }
}

// v -> int8 with a static scale per channel (inv: 127 / max), clamped to [-127, 127]
static inline int8_t q8_static(float v, float inv)
{
    const int q = pq_round(v * inv);
    return (int8_t)(q > 127 ? 127 : q < -127 ? -127 : q);
}

void pq_nonlin_attention(pieasr_model_t *m, const pq_stack_t *st, pq_layer_t *ly, const float *x, const float *attn0,
                         float *y, int acc)
{
    const int L = st->L, S = st->S, n = L + S, hd = ly->na_in.w.out / 3;
    size_t mark = m->ws_used;
    if (pieasr_opt_int && ly->na_tab && ly->nl8) {   // PIEASR_I16_NA
        const pq_lin_t *li = &ly->na_in;
        int16_t *h16 =
            (int16_t *)(((uintptr_t)pq_ws_alloc(m, ((size_t)S * 3 * hd + 8) / 2 + 4) + 15) & ~(uintptr_t)15);
        int8_t *V8 = (int8_t *)(((uintptr_t)pq_ws_alloc(m, ((size_t)n * hd + 16) / 4 + 4) + 15) & ~(uintptr_t)15);
        PQ_PROF_START();
        size_t mk = m->ws_used;
        pqi_io_t a = { .T = S, .ldx = li->w.in, .y16 = h16, .tab = ly->na_tab, .tab_row0 = 0, .tab_row1 = hd,
                       .ldy = 3 * hd, .scratch = pq_ws_alloc(m, (pq_linear_int_scratch(&li->w, S) + 3) / 4) };
        pq_io_in(m, &a, x);
        pq_linear_int_io(&li->w, &li->qi, li->b, &a);
        m->ws_used = mk;
        memcpy(V8, ly->nl8, (size_t)L * hd);
        // V8[L + t][c] = q8(v tanh(s)): (v = h[hd + c], tanh(s) = h[c] by the table)
        for (int c = 0; c < hd; c += 16) {
            pq_mul_q8(h16 + hd + c, h16 + c, 3 * hd, S, ly->na_N + c, ly->na_sh2, V8 + (size_t)L * hd + c, hd);
        }
        memcpy(ly->nl8, V8 + (size_t)S * hd, (size_t)L * hd);
        PQ_PROF_END(linear);
        float *o = pq_ws_alloc(m, (size_t)S * hd);
        prof_t0 = pieasr_clock ? pieasr_clock() : 0;
        av_job_t job = { .attn = attn0, .V8 = V8, .vs = ly->nl_s, .gate16 = h16 + 2 * hd, .gs = li->qi.sy, .o = o,
                         .S = S, .n = n, .w = hd, .ldv = hd, .ldo = hd, .ldg = 3 * hd, .heads = 1, .hstride_attn = 0 };
        PQ_TIMED(av, pq_par_run(av_part, &job));
        PQ_PROF_END(attn);
        pq_linear_p(m, &ly->na_out, o, S, y, acc ? PQI_POST_ADD : PQI_POST_NONE);
        m->ws_used = mark;
        return;
    }
    float *h = pq_ws_alloc(m, (size_t)S * 3 * hd);
    pq_linear(m, &ly->na_in, x, S, h);
    if (ly->nl8) {   // pieasr_opt_int: int8 values, static scale per channel
        int8_t *V8 = (int8_t *)pq_ws_alloc(m, ((size_t)n * hd + 3) / 4);
        memcpy(V8, ly->nl8, (size_t)L * hd);
        for (int t = 0; t < S; t++) {
            const float *r = h + (size_t)t * 3 * hd;
            for (int c = 0; c < hd; c++) {
                V8[(size_t)(L + t) * hd + c] = q8_static(r[hd + c] * pq_fm_tanh_tab(r[c]), ly->nl_is[c]);
            }
        }
        memcpy(ly->nl8, V8 + (size_t)S * hd, (size_t)L * hd);
        float *o = pq_ws_alloc(m, (size_t)S * hd);
        PQ_PROF_START();
        av_job_t job = { .attn = attn0, .V8 = V8, .vs = ly->nl_s, .gate = h + 2 * hd, .o = o, .S = S, .n = n,
                         .w = hd, .ldv = hd, .ldo = hd, .ldg = 3 * hd, .heads = 1, .hstride_attn = 0 };
        PQ_TIMED(av, pq_par_run(av_part, &job));
        PQ_PROF_END(attn);
        pq_linear_p(m, &ly->na_out, o, S, y, acc ? PQI_POST_ADD : PQI_POST_NONE);
        m->ws_used = mark;
        return;
    }
    float *V = pq_ws_alloc(m, (size_t)n * hd);
    memcpy(V, ly->nonlin, sizeof(float) * L * hd);
    for (int t = 0; t < S; t++) {
        const float *r = h + (size_t)t * 3 * hd;
        for (int c = 0; c < hd; c++) {
            V[(size_t)(L + t) * hd + c] = r[hd + c] * pq_fm_tanh_tab(r[c]);
        }
    }
    memcpy(ly->nonlin, V + (size_t)S * hd, sizeof(float) * L * hd);
    float *o = pq_ws_alloc(m, (size_t)S * hd);
    PQ_PROF_START();
    av_job_t job = { .attn = attn0, .V = V, .gate = h + 2 * hd, .o = o, .S = S, .n = n, .w = hd, .ldv = hd,
                     .ldo = hd, .ldg = 3 * hd, .heads = 1, .hstride_attn = 0 };
    PQ_TIMED(av, pq_par_run(av_part, &job));
    PQ_PROF_END(attn);
    pq_linear_p(m, &ly->na_out, o, S, y, acc ? PQI_POST_ADD : PQI_POST_NONE);
    m->ws_used = mark;
}

void pq_self_attention(pieasr_model_t *m, const pq_stack_t *st, pq_layer_t *ly, int which, const float *x,
                       const float *attn, float *y, int acc)
{
    const int H = st->H, L = st->L, S = st->S, n = L + S, wv = VD * H;
    size_t mark = m->ws_used;
    if (pieasr_opt_int && (pieasr_opt_i16 & PIEASR_I16_SA) && ly->val8[which] && ly->sa_N[which]) {
        // PIEASR_I16_SA: the product writes the int8 values directly
        const pq_lin_t *li = &ly->sa_in[which];
        int8_t *V8 = (int8_t *)(((uintptr_t)pq_ws_alloc(m, ((size_t)n * wv + 16) / 4 + 4) + 15) & ~(uintptr_t)15);
        memcpy(V8, ly->val8[which], (size_t)L * wv);
        PQ_PROF_START();
        size_t mk = m->ws_used;
        pqi_io_t a = { .T = S, .ldx = li->w.in, .y8 = V8 + (size_t)L * wv, .N8 = ly->sa_N[which],
                       .sh8 = ly->sa_sh[which], .ldy = wv,
                       .scratch = pq_ws_alloc(m, (pq_linear_int_scratch(&li->w, S) + 3) / 4) };
        pq_io_in(m, &a, x);
        pq_linear_int_io(&li->w, &li->qi, li->b, &a);
        m->ws_used = mk;
        PQ_PROF_END(linear);
        memcpy(ly->val8[which], V8 + (size_t)S * wv, (size_t)L * wv);
        float *o = pq_ws_alloc(m, (size_t)S * wv);
        prof_t0 = pieasr_clock ? pieasr_clock() : 0;
        av_job_t job = { .attn = attn, .V8 = V8, .vs = ly->val_s[which], .gate = NULL, .o = o, .S = S, .n = n,
                         .w = VD, .ldv = wv, .ldo = wv, .ldg = 0, .heads = H, .hstride_attn = S * n };
        PQ_TIMED(av, pq_par_run(av_part, &job));
        PQ_PROF_END(attn);
        pq_linear_p(m, &ly->sa_out[which], o, S, y, acc ? PQI_POST_ADD : PQI_POST_NONE);
        m->ws_used = mark;
        return;
    }
    if (ly->val8[which]) {   // pieasr_opt_int: int8 values, static scale per channel
        int8_t *V8 = (int8_t *)pq_ws_alloc(m, ((size_t)n * wv + 3) / 4);
        float *vn = pq_ws_alloc(m, (size_t)S * wv);
        memcpy(V8, ly->val8[which], (size_t)L * wv);
        pq_linear(m, &ly->sa_in[which], x, S, vn);
        const float *is = ly->val_is[which];
        for (int t = 0; t < S; t++) {
            for (int c = 0; c < wv; c++) {
                V8[(size_t)(L + t) * wv + c] = q8_static(vn[(size_t)t * wv + c], is[c]);
            }
        }
        memcpy(ly->val8[which], V8 + (size_t)S * wv, (size_t)L * wv);
        float *o = pq_ws_alloc(m, (size_t)S * wv);
        PQ_PROF_START();
        av_job_t job = { .attn = attn, .V8 = V8, .vs = ly->val_s[which], .gate = NULL, .o = o, .S = S, .n = n,
                         .w = VD, .ldv = wv, .ldo = wv, .ldg = 0, .heads = H, .hstride_attn = S * n };
        PQ_TIMED(av, pq_par_run(av_part, &job));
        PQ_PROF_END(attn);
        pq_linear_p(m, &ly->sa_out[which], o, S, y, acc ? PQI_POST_ADD : PQI_POST_NONE);
        m->ws_used = mark;
        return;
    }
    float *V = pq_ws_alloc(m, (size_t)n * wv);
    memcpy(V, ly->val[which], sizeof(float) * L * wv);
    pq_linear(m, &ly->sa_in[which], x, S, V + (size_t)L * wv);
    memcpy(ly->val[which], V + (size_t)S * wv, sizeof(float) * L * wv);
    float *o = pq_ws_alloc(m, (size_t)S * wv);
    PQ_PROF_START();
    av_job_t job = { .attn = attn, .V = V, .gate = NULL, .o = o, .S = S, .n = n, .w = VD, .ldv = wv, .ldo = wv,
                     .ldg = 0, .heads = H, .hstride_attn = S * n };
    PQ_TIMED(av, pq_par_run(av_part, &job));
    PQ_PROF_END(attn);
    pq_linear_p(m, &ly->sa_out[which], o, S, y, acc ? PQI_POST_ADD : PQI_POST_NONE);
    m->ws_used = mark;
}
