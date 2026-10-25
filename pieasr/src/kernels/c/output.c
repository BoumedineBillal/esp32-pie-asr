// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The output kernels in C (the reference of pie/output.S).
#include "kernels/kernels.h"

void pq_out_tab16(const int32_t *y32, int T, const uint32_t *tab, int16_t *h, int ldh)
{
    for (int t = 0; t < T; t++) {
        for (int i = 0; i < 16; i++) {
            int32_t v = y32[t * 16 + i] >> 8;
            v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
            h[(size_t)t * ldh + i] = tab ? pq_tab_eval(tab, v) : (int16_t)v;
        }
    }
}

void pq_out_add16(const int32_t *y32, int T, int n, int j, int16_t *x, int ldx)
{
    for (int t = 0; t < T; t++) {
        for (int i = 0; i < 16; i++) {
            int64_t s = (int64_t)x[(size_t)t * ldx + i] * (1 << (n - j)) + y32[t * 16 + i];
            s = s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : s;
            const int32_t v = (int32_t)s >> n;
            x[(size_t)t * ldx + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
}

void pq_out_act8(const int32_t *acc, int T, const int16_t *M, const int32_t *B, const int16_t *N, int r, int sh,
                 int sh2, const uint32_t *tab, int8_t *y, int ldy)
{
    for (int t = 0; t < T; t++) {
        for (int i = 0; i < 16; i++) {
            const int32_t a = pq_sat16(acc[t * 16 + i] >> r);
            int64_t s = (int64_t)a * M[i] + B[i];
            s = s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : s;
            const int32_t v = pq_sat16((int32_t)s >> sh);
            const int32_t h = tab ? pq_tab_eval_k(tab, v, PQ_ACT8_K) : v;
            const int32_t m = h * N[i];
            int32_t q = m;
            if (sh2 > 0) {
                const int32_t u = m < 0 ? -m : m;
                q = (u + (1 << (sh2 - 1))) >> sh2;
                q = m < 0 ? -q : q;
            }
            q = pq_sat16(q);
            y[(size_t)t * ldy + i] = (int8_t)(q > 127 ? 127 : q < -127 ? -127 : q);
        }
    }
}

uint32_t pq_sumsq16(const int16_t *x, const int16_t *b, int C)
{
    uint32_t s = 0;
    for (int c = 0; c < C; c++) {
        const int32_t d = pq_sat16((int32_t)x[c] - b[c]);
        s += (uint32_t)(d * d) >> 8;
    }
    return s;
}

void pq_scale_bypass16(const int16_t *x, const int16_t *xo, int C, int A, int k, const int16_t *bb, const int16_t *w,
                       int16_t *out)
{
    for (int c = 0; c < C; c++) {
        const int32_t n = pq_sat16(((int32_t)x[c] * A) >> k);
        out[c] = (int16_t)pq_sat16(((int32_t)xo[c] * w[c] + n * bb[c] + 8192) >> 14);
    }
}
