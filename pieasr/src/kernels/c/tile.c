// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The tile kernels in C (the reference of pie/tile.S).
#include <string.h>
#include "kernels/kernels.h"

void pq_tile16(const int8_t *w, const int8_t *x, int xstride, int T, int G, int32_t *acc)
{
    for (int t = 0; t < T; t++) {
        int32_t a[16] = { 0 };
        const int8_t *xt = x + (size_t)t * xstride;
        for (int k = 0; k < G; k++) {
            for (int i = 0; i < 16; i++) {
                a[i] += w[k * 16 + i] * xt[k];
            }
        }
        memcpy(acc + t * 16, a, sizeof(a));
    }
}

void pq_tile16_n(const int8_t *w, const int8_t *x, int xstride, int T, int G, const int16_t *n16, const uint8_t *sar,
                 int32_t *y32)
{
    for (int t = 0; t < T; t++) {
        const int8_t *xt = x + (size_t)t * xstride;
        for (int i = 0; i < 16; i++) {
            int32_t acc = 32;
            for (int k = 0; k < G; k++) {
                acc += w[k * 16 + i] * xt[k];
            }
            const int32_t p = ((acc >> 6) * (int32_t)n16[i]) >> sar[t];
            int64_t s = (int64_t)y32[t * 16 + i] + p;
            y32[t * 16 + i] = s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : (int32_t)s;
        }
    }
}
