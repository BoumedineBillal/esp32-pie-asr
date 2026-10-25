// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The integer Hadamard kernels in C (the reference of pie/hadamard.S).
#include "kernels/kernels.h"

// 3 butterfly stages (h, 2h, 4h) on v[0], v[h], ..., v[7h] in registers
static inline void fwht8_i(int32_t *v, int h)
{
    const int32_t a0 = v[0], a1 = v[h], a2 = v[2 * h], a3 = v[3 * h];
    const int32_t a4 = v[4 * h], a5 = v[5 * h], a6 = v[6 * h], a7 = v[7 * h];
    const int32_t b0 = a0 + a1, b1 = a0 - a1, b2 = a2 + a3, b3 = a2 - a3;
    const int32_t b4 = a4 + a5, b5 = a4 - a5, b6 = a6 + a7, b7 = a6 - a7;
    const int32_t c0 = b0 + b2, c2 = b0 - b2, c1 = b1 + b3, c3 = b1 - b3;
    const int32_t c4 = b4 + b6, c6 = b4 - b6, c5 = b5 + b7, c7 = b5 - b7;
    v[0] = c0 + c4;
    v[4 * h] = c0 - c4;
    v[h] = c1 + c5;
    v[5 * h] = c1 - c5;
    v[2 * h] = c2 + c6;
    v[6 * h] = c2 - c6;
    v[3 * h] = c3 + c7;
    v[7 * h] = c3 - c7;
}

static inline uint32_t iabs(int32_t v)
{
    return (uint32_t)((v ^ (v >> 31)) - (v >> 31));
}

// the stages 8, 16, ..., n/2 (after 3 stages done per block of 8); returns the OR of |v| (its bit length is that of
// max |v|), formed in the last stage
uint32_t pq_fwht_i32_from8(int32_t *v, int n)
{
    int h = 8;
    if (n >= 64) {
        for (int b = 0; b < n; b += 64) {
            for (int j = 0; j < 8; j++) {
                fwht8_i(v + b + j, 8);
            }
        }
        h = 64;
    }
    for (; h < n / 2; h *= 2) {
        for (int i = 0; i < n; i += 2 * h) {
            for (int j = i; j < i + h; j++) {
                const int32_t a = v[j], b = v[j + h];
                v[j] = a + b;
                v[j + h] = a - b;
            }
        }
    }
    uint32_t m0 = 0, m1 = 0;
    if (h == n / 2) {   // the last stage, with the OR
        for (int j = 0; j < h; j += 2) {
            const int32_t a0 = v[j], b0 = v[j + h], a1 = v[j + 1], b1 = v[j + 1 + h];
            const int32_t s0 = a0 + b0, d0 = a0 - b0, s1 = a1 + b1, d1 = a1 - b1;
            v[j] = s0;
            v[j + h] = d0;
            v[j + 1] = s1;
            v[j + 1 + h] = d1;
            m0 |= iabs(s0) | iabs(d0);
            m1 |= iabs(s1) | iabs(d1);
        }
    } else {            // n == 64 after the radix-8 stages: none left
        for (int j = 0; j < n; j += 2) {
            m0 |= iabs(v[j]);
            m1 |= iabs(v[j + 1]);
        }
    }
    return m0 | m1;
}

uint32_t pq_fwht_i32(int32_t *r, int G)
{
    for (int i = 0; i < G; i += 8) {
        fwht8_i(r + i, 1);
    }
    return pq_fwht_i32_from8(r, G);
}

uint32_t pq_fwht_i16(const int16_t *x, int32_t *r, int G)
{
    for (int i = 0; i < G; i++) {
        r[i] = x[i];
    }
    return pq_fwht_i32(r, G);
}
