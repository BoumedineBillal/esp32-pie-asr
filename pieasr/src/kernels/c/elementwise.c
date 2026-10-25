// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The elementwise kernels in C (the reference of pie/elementwise.S).
#include "kernels/kernels.h"

void pq_q8_shift(const int32_t *r, int n, int e, int8_t *q)
{
    for (int k = 0; k < n; k++) {
        int32_t v = r[k];
        if (e > 0) {   // round half to even
            const int32_t f = v >> e, rem = v - (int32_t)((uint32_t)f << e), half = 1 << (e - 1);
            v = f + (rem > half || (rem == half && (f & 1)));
        }
        q[k] = (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v);
    }
}

void pq_mul_q8(const int16_t *a, const int16_t *b, int lda, int T, const int16_t *N, int sh2, int8_t *y, int ldy)
{
    for (int t = 0; t < T; t++) {
        for (int i = 0; i < 16; i++) {
            const int32_t g = pq_sat16(pq_rshift_away((int32_t)a[(size_t)t * lda + i] * b[(size_t)t * lda + i], 15));
            const int32_t q = pq_sat16(pq_rshift_away(g * N[i], sh2));
            y[(size_t)t * ldy + i] = (int8_t)(q > 127 ? 127 : q < -127 ? -127 : q);
        }
    }
}

void pq_mul16(const int16_t *a, const int16_t *b, int lda, int T, int16_t *y)
{
    for (int t = 0; t < T; t++) {
        for (int i = 0; i < 16; i++) {
            const int32_t p = (int32_t)a[(size_t)t * lda + i] * b[(size_t)t * lda + i];
            y[t * 16 + i] = (int16_t)pq_sat16(pq_rshift_away(p, 15));
        }
    }
}

void pq_lane_q8(const int16_t *g, int T, const int16_t *N, int8_t *y, int ldy)
{
    for (int t = 0; t < T; t++) {
        for (int i = 0; i < 16; i++) {
            const int32_t q = pq_sat16(pq_rshift_away((int32_t)g[t * 16 + i] * N[i], 15));
            y[(size_t)t * ldy + i] = (int8_t)(q > 127 ? 127 : q < -127 ? -127 : q);
        }
    }
}

void pq_lane_or16(const int16_t *g, int T, int32_t *m)
{
    for (int i = 0; i < 16; i++) {
        m[i] = 0;
    }
    for (int t = 0; t < T; t++) {
        for (int i = 0; i < 16; i++) {
            const int32_t v = g[t * 16 + i];
            m[i] |= (v ^ (v >> 15)) & 0xffff;
        }
    }
}
