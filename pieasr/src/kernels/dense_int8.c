// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// Dense int8 matrix products on the tile kernel: the weights' tiles, the inputs quantized per frame, the outputs.
#include <math.h>
#include <string.h>
#include "kernels/dense_int8.h"
#include "kernels/kernels.h"
#include "math/fastmath.h"
#include "runtime/parallel.h"

// the longest dense input is 512 (decoder_proj, joiner); the front-end convs have 72 / 288 / 128 / 384
#define DENSE_KMAX 512

static int finish(pq_dense_t *d, const float *bias, void *(*alloc)(size_t))
{
    if (bias) {
        d->bias = alloc(sizeof(float) * d->out);
        if (!d->bias) {
            return -1;
        }
        memcpy(d->bias, bias, sizeof(float) * d->out);
    }
    return 0;
}

int pq_dense_from_f32(pq_dense_t *d, const float *w, int out, int K, int ldw, const float *bias,
                      void *(*alloc)(size_t))
{
    memset(d, 0, sizeof(*d));
    d->out = out;
    d->K = K;
    d->Kp = (K + 15) / 16 * 16;
    if (d->Kp > DENSE_KMAX) {
        return -3;
    }
    // 16-byte aligned: the PIE loads ignore the low 4 address bits
    d->w = (int8_t *)(((uintptr_t)alloc((size_t)(out + 15) / 16 * 16 * d->Kp + 32 + 15) + 15) & ~(uintptr_t)15);
    d->wscale = alloc(sizeof(float) * out);
    if (!d->w || !d->wscale) {
        return -1;
    }
    memset(d->w, 0, (size_t)(out + 15) / 16 * 16 * d->Kp + 32);
    for (int o = 0; o < out; o++) {
        float amax = 0.0f;
        for (int k = 0; k < K; k++) {
            amax = fabsf(w[(size_t)o * ldw + k]) > amax ? fabsf(w[(size_t)o * ldw + k]) : amax;
        }
        float inv = amax > 0.0f ? 127.0f / amax : 0.0f;
        d->wscale[o] = amax / 127.0f;
        int8_t *tile = d->w + (size_t)(o / 16) * d->Kp * 16;
        for (int k = 0; k < K; k++) {
            tile[k * 16 + (o & 15)] = (int8_t)pq_round(w[(size_t)o * ldw + k] * inv);
        }
    }
    return finish(d, bias, alloc);
}

size_t pq_dense_bytes(int out, int K)
{
    return (size_t)(out + 15) / 16 * 16 * ((K + 15) / 16 * 16) + 32 + 15;
}

int pq_dense_from_q8(pq_dense_t *d, const uint8_t *q, int is_uint8, int transposed, int out, int K, float scale,
                     int zp, const float *bias, void *(*alloc)(size_t))
{
    if (is_uint8 || zp != 0) {   // not representable as is: through float
        return -2;
    }
    memset(d, 0, sizeof(*d));
    d->out = out;
    d->K = K;
    d->Kp = (K + 15) / 16 * 16;
    if (d->Kp > DENSE_KMAX) {
        return -3;
    }
    // 16-byte aligned: the PIE loads ignore the low 4 address bits
    d->w = (int8_t *)(((uintptr_t)alloc((size_t)(out + 15) / 16 * 16 * d->Kp + 32 + 15) + 15) & ~(uintptr_t)15);
    d->wscale = alloc(sizeof(float) * out);
    if (!d->w || !d->wscale) {
        return -1;
    }
    memset(d->w, 0, (size_t)(out + 15) / 16 * 16 * d->Kp + 32);
    for (int o = 0; o < out; o++) {
        d->wscale[o] = scale;
        int8_t *tile = d->w + (size_t)(o / 16) * d->Kp * 16;
        for (int k = 0; k < K; k++) {
            tile[k * 16 + (o & 15)] = (int8_t)q[transposed ? (size_t)k * out + o : (size_t)o * K + k];
        }
    }
    return finish(d, bias, alloc);
}

pq_dense_prof_t pq_dense_prof;

#define DT 64   // frames per block
static int8_t xq_blk[DT * DENSE_KMAX + 32] __attribute__((aligned(16)));
static float xs_blk[DT];
static int32_t acc_blk[PQ_PAR_PARTS][DT * 16] __attribute__((aligned(16)));   // per core

typedef struct {
    const pq_dense_t *d;
    const float *x;
    const int8_t *xq;      // int8 input (or NULL: quantize x per frame)
    float xscale;
    float *y;
    int8_t *yq;            // int8 output (or NULL: float y)
    const float *inv_ys;   // its scale per output (inverse)
    int t0, n, ldx, ldy;
    int post;
} dense_job_t;

// phase 1: quantize this part's frames of the block
static void dense_quant_part(void *ctx, int part, int parts)
{
    const dense_job_t *j = ctx;
    const int K = j->d->K, Kp = j->d->Kp;
    int a, b;
    pq_par_range(j->n, part, parts, 1, &a, &b);
    const uint32_t c0 = pq_cycles();
    for (int t = a; t < b; t++) {
        const float *xr = j->x + (size_t)(j->t0 + t) * j->ldx;
        float amax = 0.0f;
        for (int k = 0; k < K; k++) {
            amax = pq_fmax(amax, fabsf(xr[k]));
        }
        float inv = amax > 0.0f ? 127.0f / amax : 0.0f;
        xs_blk[t] = amax / 127.0f;
        int8_t *q = xq_blk + (size_t)t * Kp;
        for (int k = 0; k < K; k++) {
            q[k] = (int8_t)pq_round(xr[k] * inv);
        }
        for (int k = K; k < Kp; k++) {
            q[k] = 0;
        }
    }
    if (part == 0) {
        pq_dense_prof.quant += pq_cycles() - c0;
    }
}

// phase 2: this part's tiles of 16 outputs. Per row block: the 16 row scales and biases in registers; per frame
// v = (s[i] * xs[t]) * acc + b[i], then post, then a float or int8 store
static void dense_tiles_part(void *ctx, int part, int parts)
{
    const dense_job_t *j = ctx;
    const pq_dense_t *d = j->d;
    const int Kp = d->Kp, post = j->post;
    int32_t *acc = acc_blk[part];
    const int8_t *xq = j->xq ? j->xq + (size_t)j->t0 * Kp : xq_blk;
    int rb0, rb1;
    pq_par_range((d->out + 15) / 16, part, parts, 1, &rb0, &rb1);
    uint32_t ct = 0, cp = 0;
    for (int rb = rb0; rb < rb1; rb++) {
        const uint32_t c0 = pq_cycles();
        pq_tile16(d->w + (size_t)rb * Kp * 16, xq, Kp, j->n, Kp, acc);
        const uint32_t c1 = pq_cycles();
        ct += c1 - c0;
        const int nv = d->out - rb * 16 < 16 ? d->out - rb * 16 : 16;
        float sc[16], bs[16];
        for (int i = 0; i < 16; i++) {
            sc[i] = i < nv ? d->wscale[rb * 16 + i] : 0.0f;
            bs[i] = i < nv && d->bias ? d->bias[rb * 16 + i] : 0.0f;
        }
        for (int t = 0; t < j->n; t++) {
            const float xs = j->xq ? j->xscale : xs_blk[t];
            const int32_t *a = acc + t * 16;
            float v[16];
            // one loop per activation: gcc does not hoist a test of `post` out of the loop
            if (post == PQ_DENSE_POST_SWOOSH_L) {
#pragma GCC unroll 16
                for (int i = 0; i < 16; i++) {
                    v[i] = pq_fm_swoosh_l_tab((sc[i] * xs) * (float)a[i] + bs[i]);
                }
            } else if (post == PQ_DENSE_POST_SWOOSH_R) {
#pragma GCC unroll 16
                for (int i = 0; i < 16; i++) {
                    v[i] = pq_fm_swoosh_r_tab((sc[i] * xs) * (float)a[i] + bs[i]);
                }
            } else {
#pragma GCC unroll 16
                for (int i = 0; i < 16; i++) {
                    v[i] = (sc[i] * xs) * (float)a[i] + bs[i];
                }
            }
            if (j->yq) {
                int8_t *yr = j->yq + (size_t)(j->t0 + t) * j->ldy + rb * 16;
                for (int i = 0; i < nv; i++) {
                    const int q = pq_round(v[i] * j->inv_ys[rb * 16 + i]);
                    yr[i] = (int8_t)(q > 127 ? 127 : q < -127 ? -127 : q);
                }
            } else {
                float *yr = j->y + (size_t)(j->t0 + t) * j->ldy + rb * 16;
                if (nv == 16) {
                    memcpy(yr, v, sizeof(v));
                } else {
                    memcpy(yr, v, sizeof(float) * nv);
                }
            }
        }
        cp += pq_cycles() - c1;
    }
    if (part == 0) {
        pq_dense_prof.tile += ct;
        pq_dense_prof.post += cp;
        pq_dense_prof.calls++;
    }
}

void pq_dense_apply_ex(const pq_dense_t *d, const float *x, const int8_t *xq, float xscale, int T, int ldx, float *y,
                       int8_t *yq, const float *inv_ys, int ldy, int post)
{
    dense_job_t j = { .d = d, .x = x, .xq = xq, .xscale = xscale, .y = y, .yq = yq, .inv_ys = inv_ys,
                      .ldx = ldx, .ldy = ldy, .post = post };
    for (j.t0 = 0; j.t0 < T; j.t0 += DT) {
        j.n = T - j.t0 < DT ? T - j.t0 : DT;
        if (!xq) {
            pq_par_run(dense_quant_part, &j);
        }
        pq_par_run(dense_tiles_part, &j);
    }
}

void pq_dense_apply(const pq_dense_t *d, const float *x, int T, int ldx, float *y, int ldy, int post)
{
    pq_dense_apply_ex(d, x, NULL, 0.0f, T, ldx, y, NULL, NULL, ldy, post);
}
