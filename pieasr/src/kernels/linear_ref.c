// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The 2/4-bit products in float (the reference) and in int8 (its plain C form); the float Hadamard transform.
#include <math.h>
#include <string.h>
#include "kernels/kernels.h"
#include "kernels/linear.h"
#include "math/tables/tables.h"

// int8 levels: round(level * 127 / max|level|) (max|level| = the last level for both tables)
const int8_t pq_lq2[4] = { -127, -38, 38, 127 };
const int8_t pq_lq4[16] = { -126, -95, -74, -57, -43, -30, -17, -5, 7, 19, 31, 45, 59, 76, 97, 127 };
float pq_lsc2 = 1.50993262f / 127.0f, pq_lsc4 = 2.74101413f / 127.0f;

size_t pq_linear_q8_scratch(const pq_cqmat_t *w, int T)
{
    return ((size_t)T * w->in + 3) / 4 * 4 + sizeof(float) * (size_t)T * (w->in / w->G);
}

void pq_linear_q8(const pq_cqmat_t *w, const uint16_t *bias, const float *x, int T, int ldx, float *y, int ldy,
                  void *scratch)
{
    const int G = w->G, in = w->in, ng = in / G;
    const int8_t *lq = w->bits == 2 ? pq_lq2 : pq_lq4;
    const float lsc = w->bits == 2 ? pq_lsc2 : pq_lsc4;
    int8_t *xq = scratch;
    float *sx = (float *)((uint8_t *)scratch + ((size_t)T * in + 3) / 4 * 4);
    float r[128];
    for (int t = 0; t < T; t++) {
        for (int g = 0; g < ng; g++) {
            memcpy(r, x + (size_t)t * ldx + g * G, sizeof(float) * G);
            pq_fwht_f32(r, G);
            float amax = 0.0f;
            for (int k = 0; k < G; k++) {
                float a = fabsf(r[k]);
                amax = a > amax ? a : amax;
            }
            float s = amax / 127.0f, inv = amax > 0.0f ? 127.0f / amax : 0.0f;
            sx[t * ng + g] = s;
            for (int k = 0; k < G; k++) {
                xq[(size_t)t * in + g * G + k] = (int8_t)pq_round(r[k] * inv);
            }
        }
    }
    int8_t wl[128];
    for (int o = 0; o < w->out; o++) {
        const float b = bias ? pq_f16_to_f32(bias[o]) : 0.0f;
        for (int t = 0; t < T; t++) {
            y[(size_t)t * ldy + o] = 0.0f;
        }
        for (int g = 0; g < ng; g++) {
            for (int k = 0; k < G; k++) {
                wl[k] = lq[pq_cq_code(w, o, g * G + k)];
            }
            const float s = pq_f16_to_f32(w->norms[(size_t)o * ng + g]) / (float)G * lsc;
            for (int t = 0; t < T; t++) {
                const int8_t *q = xq + (size_t)t * in + g * G;
                int32_t acc = 0;
                for (int k = 0; k < G; k++) {
                    acc += wl[k] * q[k];
                }
                y[(size_t)t * ldy + o] += (s * sx[t * ng + g]) * (float)acc;
            }
        }
        for (int t = 0; t < T; t++) {
            y[(size_t)t * ldy + o] += b;
        }
    }
}

// three butterfly stages (h, 2h, 4h) on 8 values in registers: the same additions, in the same order per element,
// as three passes of the generic loop
static inline void fwht8(float *v, int h)
{
    float a0 = v[0], a1 = v[h], a2 = v[2 * h], a3 = v[3 * h], a4 = v[4 * h], a5 = v[5 * h], a6 = v[6 * h],
          a7 = v[7 * h];
    float b0 = a0 + a1, b1 = a0 - a1, b2 = a2 + a3, b3 = a2 - a3, b4 = a4 + a5, b5 = a4 - a5, b6 = a6 + a7,
          b7 = a6 - a7;   // stage h
    float c0 = b0 + b2, c2 = b0 - b2, c1 = b1 + b3, c3 = b1 - b3, c4 = b4 + b6, c6 = b4 - b6, c5 = b5 + b7,
          c7 = b5 - b7;   // stage 2h
    v[0] = c0 + c4;
    v[4 * h] = c0 - c4;   // stage 4h
    v[h] = c1 + c5;
    v[5 * h] = c1 - c5;
    v[2 * h] = c2 + c6;
    v[6 * h] = c2 - c6;
    v[3 * h] = c3 + c7;
    v[7 * h] = c3 - c7;
}

void pq_fwht_f32(float *v, int n)
{
    if (n == 128 || n == 64) {   // stages 1, 2, 4 | 8, 16, 32 | (64)
        for (int i = 0; i < n; i += 8) {
            fwht8(v + i, 1);
        }
        for (int b = 0; b < n; b += 64) {
            for (int j = 0; j < 8; j++) {
                fwht8(v + b + j, 8);
            }
        }
        if (n == 128) {
            for (int j = 0; j < 64; j++) {
                float a = v[j], c = v[j + 64];
                v[j] = a + c;
                v[j + 64] = a - c;
            }
        }
        return;
    }
    // stages h = 1 and 2 together (radix 4): the same additions in the same order as the generic loop below
    int h = 1;
    if (n >= 4) {
        for (int i = 0; i < n; i += 4) {
            float a = v[i], b = v[i + 1], c = v[i + 2], d = v[i + 3];
            float ab = a + b, amb = a - b, cd = c + d, cmd = c - d;
            v[i] = ab + cd;
            v[i + 1] = amb + cmd;
            v[i + 2] = ab - cd;
            v[i + 3] = amb - cmd;
        }
        h = 4;
    }
    // then two stages at a time (h, 2h): radix 4 over the strided elements (independent butterflies: unrolled so
    // the in-order core overlaps their adds)
    for (; 2 * h < n; h <<= 2) {
        for (int i = 0; i < n; i += 4 * h) {
#pragma GCC unroll 4
            for (int j = i; j < i + h; j++) {
                float a = v[j], b = v[j + h], c = v[j + 2 * h], d = v[j + 3 * h];
                float ab = a + b, amb = a - b, cd = c + d, cmd = c - d;
                v[j] = ab + cd;
                v[j + h] = amb + cmd;
                v[j + 2 * h] = ab - cd;
                v[j + 3 * h] = amb - cmd;
            }
        }
    }
    for (; h < n; h <<= 1) {
        for (int i = 0; i < n; i += 2 * h) {
            for (int j = i; j < i + h; j++) {
                float a = v[j], b = v[j + h];
                v[j] = a + b;
                v[j + h] = a - b;
            }
        }
    }
}

// the reference product: plain C, float32, one decoded group of levels reused for all T inputs
void pq_linear_ref(const pq_cqmat_t *w, const uint16_t *bias, const float *x, int T, int ldx, float *y, int ldy,
                   float *scratch)
{
    const int G = w->G, in = w->in, ng = in / G;
    const float *lv = w->bits == 2 ? pq_levels2 : pq_levels4;
    // rotated inputs, [T][in]
    for (int t = 0; t < T; t++) {
        float *r = scratch + (size_t)t * in;
        memcpy(r, x + (size_t)t * ldx, sizeof(float) * in);
        for (int g = 0; g < ng; g++) {
            pq_fwht_f32(r + g * G, G);
        }
    }
    float wl[128];
    for (int o = 0; o < w->out; o++) {
        const float b = bias ? pq_f16_to_f32(bias[o]) : 0.0f;
        for (int t = 0; t < T; t++) {
            y[(size_t)t * ldy + o] = 0.0f;
        }
        for (int g = 0; g < ng; g++) {
            for (int k = 0; k < G; k++) {
                wl[k] = lv[pq_cq_code(w, o, g * G + k)];
            }
            const float s = pq_f16_to_f32(w->norms[(size_t)o * ng + g]) / (float)G;
            for (int t = 0; t < T; t++) {
                const float *r = scratch + (size_t)t * in + g * G;
                float acc = 0.0f;
                for (int k = 0; k < G; k++) {
                    acc += wl[k] * r[k];
                }
                y[(size_t)t * ldy + o] += s * acc;
            }
        }
        for (int t = 0; t < T; t++) {
            y[(size_t)t * ldy + o] += b;
        }
    }
}
