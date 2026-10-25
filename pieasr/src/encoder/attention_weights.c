// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The attention weights: q.k plus the relative position term, masked, then softmax per row (float, int8 or integer).
#include <math.h>
#include <string.h>
#include "encoder/encoder_internal.h"
#include "math/fastmath.h"

int8_t *pq_att8;
float *pq_att8_s;
int pq_att8_np;
pq_av_buf_t pq_avq[PQ_PAR_PARTS];

// the attention scores and softmax, rows (h, t) split between the cores
typedef struct {
    const pq_stack_t *st;
    const pq_layer_t *ly;
    const float *proj, *K;
    const int8_t *K8;      // pieasr_opt_int: int8 keys [n][32H] with scales KS [n][H] (K unused)
    const float *KS;
    const int8_t *Q8;      // PIEASR_I16_AW: aw_in's int8 output [S][68H] (q, k, pq); K8 at the static scale, KS unused
    const uint8_t *mask;
    float *attn;
} scores_job_t;

// softmax of x [n] as int8 (pieasr_opt_int): e_j = exp(x_j - max) (the max element: exactly 1), q_j = round(127 e_j),
// the row's scale 1 / (127 sum e) (q * scale = the softmax value); q zero padded to np
static void softmax_q8(const float *x, int n, int np, int8_t *q, float *scale)
{
    float mx = x[0];
    for (int i = 1; i < n; i++) {
        mx = pq_fmax(mx, x[i]);
    }
    float s0 = 0.0f, s1 = 0.0f;
    int i = 0;
    for (; i + 2 <= n; i += 2) {
        const float e0 = pq_fm_exp_neg_tab(x[i] - mx), e1 = pq_fm_exp_neg_tab(x[i + 1] - mx);
        s0 += e0;
        s1 += e1;
        q[i] = (int8_t)pq_round(e0 * 127.0f);
        q[i + 1] = (int8_t)pq_round(e1 * 127.0f);
    }
    for (; i < n; i++) {
        const float e = pq_fm_exp_neg_tab(x[i] - mx);
        s0 += e;
        q[i] = (int8_t)pq_round(e * 127.0f);
    }
    for (int k = n; k < np; k++) {
        q[k] = 0;
    }
    *scale = 1.0f / (127.0f * (s0 + s1));
}

static void softmax_inplace(float *x, int n)
{
    float mx = x[0];
    for (int i = 1; i < n; i++) {
        mx = pq_fmax(mx, x[i]);
    }
    float s = 0.0f;
    for (int i = 0; i < n; i++) {
        x[i] = pq_fm_exp_neg_tab(x[i] - mx);   // x - max <= 0: the table
        s += x[i];
    }
    const float inv = 1.0f / s;   // one division per row
    for (int i = 0; i < n; i++) {
        x[i] *= inv;
    }
}

// per-core internal-RAM buffer of the scores loop: one head's queries [S][32]
static float att_q[PQ_PAR_PARTS][64 * QD];

// q.k on the tile kernel: queries int8 with one scale per row, keys int8 with one scale per key; per 16 keys a tile
// [32][16] of K^T and pq_tile16. Masked keys: -1000 (as the float loop).
static void scores_qk_q8(const scores_job_t *jb, const float *qb, int S, int n, int koff, int wq, float *sch,
                         const pq_av_buf_t *b)
{
    for (int t = 0; t < S; t++) {
        const float *q = qb + t * QD;
        float amax = 0.0f;
        for (int d = 0; d < QD; d++) {
            amax = pq_fmax(amax, fabsf(q[d]));
        }
        const float inv = amax > 0.0f ? 127.0f / amax : 0.0f;
        b->xs[t] = amax / 127.0f;
        int8_t *qq = b->xq + t * QD;
        for (int d = 0; d < QD; d++) {
            qq[d] = (int8_t)pq_round(q[d] * inv);
        }
    }
    for (int j0 = 0; j0 < n; j0 += 16) {
        const int cw = n - j0 < 16 ? n - j0 : 16;
        float ks[16];
        for (int i = 0; i < 16; i++) {
            const int j = j0 + i;
            if (i >= cw || jb->mask[j]) {
                ks[i] = 0.0f;
                for (int d = 0; d < QD; d++) {
                    b->vt[d * 16 + i] = 0;
                }
                continue;
            }
            const float *k = jb->K + (size_t)j * wq + koff;
            float amax = 0.0f;
            for (int d = 0; d < QD; d++) {
                amax = pq_fmax(amax, fabsf(k[d]));
            }
            const float inv = amax > 0.0f ? 127.0f / amax : 0.0f;
            ks[i] = amax / 127.0f;
            for (int d = 0; d < QD; d++) {
                b->vt[d * 16 + i] = (int8_t)pq_round(k[d] * inv);
            }
        }
        pq_tile16(b->vt, b->xq, QD, S, QD, b->acc);
        for (int t = 0; t < S; t++) {
            float *sc = sch + (size_t)t * n + j0;
            const float sqt = b->xs[t];
            const int32_t *a = b->acc + t * 16;
            for (int i = 0; i < cw; i++) {
                sc[i] = jb->mask[j0 + i] ? -1000.0f : (ks[i] * sqt) * (float)a[i];
            }
        }
    }
}

// q.k with the int8 key cache (pieasr_opt_int): the same product as scores_qk_q8, whose per-key quantization was done
// once when each key was produced (identical values), read here without any float pass
static void scores_qk_q8i(const scores_job_t *jb, const float *qb, int S, int n, int h, int H, float *sch,
                          const pq_av_buf_t *b)
{
    const int wq = QD * H;
    for (int t = 0; t < S; t++) {
        const float *q = qb + t * QD;
        float amax = 0.0f;
        for (int d = 0; d < QD; d++) {
            amax = pq_fmax(amax, fabsf(q[d]));
        }
        const float inv = amax > 0.0f ? 127.0f / amax : 0.0f;
        b->xs[t] = amax / 127.0f;
        int8_t *qq = b->xq + t * QD;
        for (int d = 0; d < QD; d++) {
            qq[d] = (int8_t)pq_round(q[d] * inv);
        }
    }
    for (int j0 = 0; j0 < n; j0 += 16) {
        const int cw = n - j0 < 16 ? n - j0 : 16;
        float ks[16];
        for (int i = 0; i < 16; i++) {
            const int j = j0 + i;
            if (i >= cw || jb->mask[j]) {
                ks[i] = 0.0f;
                for (int d = 0; d < QD; d++) {
                    b->vt[d * 16 + i] = 0;
                }
                continue;
            }
            ks[i] = jb->KS[(size_t)j * H + h];
            const int8_t *k8 = jb->K8 + (size_t)j * wq + h * QD;
            for (int d = 0; d < QD; d++) {
                b->vt[d * 16 + i] = k8[d];
            }
        }
        pq_tile16(b->vt, b->xq, QD, S, QD, b->acc);
        for (int t = 0; t < S; t++) {
            float *sc = sch + (size_t)t * n + j0;
            const float sqt = b->xs[t];
            const int32_t *a = b->acc + t * 16;
            for (int i = 0; i < cw; i++) {
                sc[i] = jb->mask[j0 + i] ? -1000.0f : (ks[i] * sqt) * (float)a[i];
            }
        }
    }
}

// PIEASR_I16_AW: the scores and softmax of head h in integers (pq_layer_t aw_*)
static void scores_int(const scores_job_t *jb, int h, const pq_av_buf_t *b)
{
    const pq_layer_t *ly = jb->ly;
    const int H = jb->st->H, L = jb->st->L, S = jb->st->S, n = L + S, wq = QD * H, W = 68 * H, np = pq_att8_np;
    for (int t = 0; t < S; t++) {
        memcpy(b->xq + t * QD, jb->Q8 + (size_t)t * W + h * QD, QD);
    }
    for (int j0 = 0; j0 < n; j0 += 16) {
        const int cw = n - j0 < 16 ? n - j0 : 16;
        for (int i = 0; i < 16; i++) {
            const int j = j0 + i;
            for (int d = 0; d < QD; d++) {
                b->vt[d * 16 + i] = i < cw && !jb->mask[j] ? jb->K8[(size_t)j * wq + h * QD + d] : 0;
            }
        }
        pq_tile16(b->vt, b->xq, QD, S, QD, b->acc);
        for (int t = 0; t < S; t++) {
            const int8_t *pq = jb->Q8 + (size_t)t * W + 2 * wq + h * PD;
            for (int i = 0; i < cw; i++) {
                const int j = j0 + i;
                const int8_t *p = ly->pos8 + (size_t)(S - 1 - t + j) * 4 * H + h * PD;
                const int32_t bb = pq[0] * p[0] + pq[1] * p[1] + pq[2] * p[2] + pq[3] * p[3];
                int32_t a = b->acc[t * 16 + i] >> ly->aw_r1, c = bb >> 1;
                a = a > 32767 ? 32767 : a < -32768 ? -32768 : a;
                int64_t s = (int64_t)a * ly->aw_Mqk + (int64_t)c * ly->aw_Mpos[h] + (1 << (ly->aw_sh2 - 1));
                s = s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : s;
                const int32_t v = (int32_t)s >> ly->aw_sh2;
                b->v16[(size_t)t * np + j] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
        }
    }
    for (int t = 0; t < S; t++) {
        const int16_t *v = b->v16 + (size_t)t * np;
        int32_t mx = -32768;
        for (int j = 0; j < n; j++) {
            mx = !jb->mask[j] && v[j] > mx ? v[j] : mx;
        }
        int8_t *q = pq_att8 + ((size_t)h * S + t) * np;
        int32_t sum = 0;
        for (int j = 0; j < n; j++) {
            int32_t d = (int32_t)v[j] - mx;
            d = d < -32768 ? -32768 : d;
            const int32_t e = jb->mask[j] ? 0 : pq_tab_eval_k(ly->aw_exp, d, PQ_ACT8_K);
            q[j] = (int8_t)e;
            sum += e;
        }
        for (int j = n; j < np; j++) {
            q[j] = 0;
        }
        pq_att8_s[h * S + t] = sum > 0 ? 1.0f / (float)sum : 0.0f;
    }
}

// heads split between the cores; per head the keys are read once (key-major loop), queries in internal RAM:
// score[t][j] = q_t . k_j (+ the position term), masked scores = -1000, then softmax per row. Every sum is formed
// in the same order as the reference loop (d ascending; then a + b), so the values are identical.
static void scores_part(void *ctx, int part, int parts)
{
    const scores_job_t *jb = ctx;
    if (jb->Q8) {
        int h0, h1;
        pq_par_range(jb->st->H, part, parts, 1, &h0, &h1);
        for (int h = h0; h < h1; h++) {
            scores_int(jb, h, &pq_avq[part]);
        }
        return;
    }
    const int H = jb->st->H, L = jb->st->L, S = jb->st->S, n = L + S, wq = QD * H;
    float *qb = att_q[part];
    int h0, h1;
    pq_par_range(H, part, parts, 1, &h0, &h1);
    for (int h = h0; h < h1; h++) {
        for (int t = 0; t < S; t++) {
            memcpy(qb + t * QD, jb->proj + (size_t)t * 68 * H + h * QD, sizeof(float) * QD);
        }
        float *sch = jb->attn + (size_t)h * S * n;
        uint64_t tq0 = (part == 0 && pieasr_clock) ? pieasr_clock() : 0;
        if (jb->K8) {
            scores_qk_q8i(jb, qb, S, n, h, H, sch, &pq_avq[part]);
        } else if (pieasr_opt_q8 == 2 && pq_avq[part].xq) {
            scores_qk_q8(jb, qb, S, n, h * QD, wq, sch, &pq_avq[part]);
        } else
        for (int j = 0; j < n; j++) {
            if (jb->mask[j]) {
                for (int t = 0; t < S; t++) {
                    sch[(size_t)t * n + j] = -1000.0f;
                }
                continue;
            }
            float k[QD];
            memcpy(k, jb->K + (size_t)j * wq + h * QD, sizeof(k));
            // 4 queries at a time: 4 independent sums, each over d in order
            int t = 0;
            for (; t + 4 <= S; t += 4) {
                const float *q0 = qb + t * QD, *q1 = q0 + QD, *q2 = q1 + QD, *q3 = q2 + QD;
                float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
                for (int d = 0; d < QD; d++) {
                    a0 += q0[d] * k[d];
                    a1 += q1[d] * k[d];
                    a2 += q2[d] * k[d];
                    a3 += q3[d] * k[d];
                }
                sch[(size_t)t * n + j] = a0;
                sch[(size_t)(t + 1) * n + j] = a1;
                sch[(size_t)(t + 2) * n + j] = a2;
                sch[(size_t)(t + 3) * n + j] = a3;
            }
            for (; t < S; t++) {
                const float *q = qb + t * QD;
                float a = 0.0f;
                for (int d = 0; d < QD; d++) {
                    a += q[d] * k[d];
                }
                sch[(size_t)t * n + j] = a;
            }
        }
        uint64_t tq1 = (part == 0 && pieasr_clock) ? pieasr_clock() : 0;
        for (int t = 0; t < S; t++) {
            const float *pq = jb->proj + (size_t)t * 68 * H + 2 * wq + h * PD;
            const float *pos = jb->ly->pos + (size_t)(S - 1 - t) * 4 * H + h * PD;
            float *sc = sch + (size_t)t * n;
            for (int j = 0; j < n; j++) {
                if (jb->mask[j]) {
                    continue;
                }
                const float *p = pos + (size_t)j * 4 * H;
                float b = 0.0f;
                for (int d = 0; d < PD; d++) {
                    b += pq[d] * p[d];
                }
                sc[j] = sc[j] + b;
            }
            if (pq_att8) {
                softmax_q8(sc, n, pq_att8_np, pq_att8 + ((size_t)h * S + t) * pq_att8_np, pq_att8_s + h * S + t);
            } else {
                softmax_inplace(sc, n);
            }
        }
        if (part == 0 && pieasr_clock) {
            uint64_t tq2 = pieasr_clock();
            pieasr_prof.sc_qk += tq1 - tq0;
            pieasr_prof.sc_pos += tq2 - tq1;
        }
    }
}

void pq_attention_weights(pieasr_model_t *m, const pq_stack_t *st, pq_layer_t *ly, const float *x, const uint8_t *mask,
                          float *attn)
{
    const int H = st->H, L = st->L, S = st->S, n = L + S, wq = QD * H, np_ = L + 2 * S - 1;
    size_t mark = m->ws_used;
    if (pieasr_opt_int && (pieasr_opt_i16 & PIEASR_I16_AW) && ly->aw_N && ly->key8 && pq_att8) {
        const pq_lin_t *li = &ly->aw_in;
        const int W = 68 * H;
        int8_t *y8 = (int8_t *)(((uintptr_t)pq_ws_alloc(m, ((size_t)S * W + 16) / 4 + 4) + 15) & ~(uintptr_t)15);
        int8_t *K8 = (int8_t *)(((uintptr_t)pq_ws_alloc(m, ((size_t)n * wq + 16) / 4 + 4) + 15) & ~(uintptr_t)15);
        PQ_PROF_START();
        size_t mk = m->ws_used;
        pqi_io_t a = { .T = S, .ldx = li->w.in, .y8 = y8, .N8 = ly->aw_N, .sh8 = ly->aw_sh, .ldy = W,
                       .scratch = pq_ws_alloc(m, (pq_linear_int_scratch(&li->w, S) + 3) / 4) };
        pq_io_in(m, &a, x);
        pq_linear_int_io(&li->w, &li->qi, li->b, &a);
        m->ws_used = mk;
        PQ_PROF_END(linear);
        memcpy(K8, ly->key8, (size_t)L * wq);
        for (int t = 0; t < S; t++) {
            memcpy(K8 + (size_t)(L + t) * wq, y8 + (size_t)t * W + wq, wq);
        }
        memcpy(ly->key8, K8 + (size_t)S * wq, (size_t)L * wq);
        prof_t0 = pieasr_clock ? pieasr_clock() : 0;
        scores_job_t job = { .st = st, .ly = ly, .K8 = K8, .Q8 = y8, .mask = mask, .attn = attn };
        pq_par_run(scores_part, &job);
        PQ_PROF_END(attn);
        (void)np_;
        m->ws_used = mark;
        return;
    }
    float *proj = pq_ws_alloc(m, (size_t)S * 68 * H);
    pq_linear(m, &ly->aw_in, x, S, proj);
    if (ly->key8) {   // pieasr_opt_int: int8 keys, each quantized once (per key and head, max / 127) when produced
        int8_t *K8 = (int8_t *)pq_ws_alloc(m, ((size_t)n * wq + 3) / 4);
        float *KS = pq_ws_alloc(m, (size_t)n * H);
        memcpy(K8, ly->key8, (size_t)L * wq);
        memcpy(KS, ly->ks, sizeof(float) * L * H);
        for (int t = 0; t < S; t++) {
            for (int h = 0; h < H; h++) {
                const float *k = proj + (size_t)t * 68 * H + wq + h * QD;
                float amax = 0.0f;
                for (int d = 0; d < QD; d++) {
                    amax = pq_fmax(amax, fabsf(k[d]));
                }
                const float inv = amax > 0.0f ? 127.0f / amax : 0.0f;
                KS[(size_t)(L + t) * H + h] = amax / 127.0f;
                int8_t *k8 = K8 + (size_t)(L + t) * wq + h * QD;
                for (int d = 0; d < QD; d++) {
                    k8[d] = (int8_t)pq_round(k[d] * inv);
                }
            }
        }
        memcpy(ly->key8, K8 + (size_t)S * wq, (size_t)L * wq);
        memcpy(ly->ks, KS + (size_t)S * H, sizeof(float) * L * H);
        PQ_PROF_START();
        scores_job_t job = { .st = st, .ly = ly, .proj = proj, .K8 = K8, .KS = KS, .mask = mask, .attn = attn };
        pq_par_run(scores_part, &job);
        PQ_PROF_END(attn);
        m->ws_used = mark;
        return;
    }
    // keys: K = concat(cache [L, 32H], k [S, 32H])
    float *K = pq_ws_alloc(m, (size_t)n * wq);
    memcpy(K, ly->key, sizeof(float) * L * wq);
    for (int t = 0; t < S; t++) {
        memcpy(K + (size_t)(L + t) * wq, proj + (size_t)t * 68 * H + wq, sizeof(float) * wq);
    }
    memcpy(ly->key, K + (size_t)S * wq, sizeof(float) * L * wq);
    PQ_PROF_START();
    scores_job_t job = { .st = st, .ly = ly, .proj = proj, .K = K, .mask = mask, .attn = attn };
    pq_par_run(scores_part, &job);
    PQ_PROF_END(attn);
    (void)np_;
    m->ws_used = mark;
}
