// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The depthwise kernels in C (the reference of pie/depthwise.S).
#include <string.h>
#include "kernels/kernels.h"

void pq_dw16(const int8_t *x, int ostep, int T, const int32_t *off, const int8_t *w, int ntaps, int32_t *acc)
{
    for (int t = 0; t < T; t++) {
        int32_t a[16] = { 0 };
        for (int k = 0; k < ntaps; k++) {
            const int8_t *xk = x + (size_t)t * ostep + off[k];
            for (int i = 0; i < 16; i++) {
                a[i] += w[k * 16 + i] * xk[i];
            }
        }
        memcpy(acc + t * 16, a, sizeof(a));
    }
}

void pq_dwcomb16(const int32_t *accc, const int32_t *acck, int T, const pq_comb_t *P, int r, int sh, const int16_t *es,
                 const uint32_t *tab, int16_t *y, int ldy)
{
    for (int t = 0; t < T; t++) {
        for (int i = 0; i < 16; i++) {
            int64_t c = (int64_t)pq_sat16(accc[t * 16 + i] >> r) * P->Mc[i] + P->Bc[i];
            int64_t k = (int64_t)pq_sat16(acck[t * 16 + i] >> r) * P->Mk[i] + P->Bk[i];
            c = c > INT32_MAX ? INT32_MAX : c < INT32_MIN ? INT32_MIN : c;
            k = k > INT32_MAX ? INT32_MAX : k < INT32_MIN ? INT32_MIN : k;
            const int32_t yc = pq_sat16((int32_t)c >> sh), yk = pq_sat16((int32_t)k >> sh);
            const int32_t v = pq_sat16(yc + pq_sat16(pq_rshift_away(yk * es[t * 16 + i], 11)));
            y[(size_t)t * ldy + i] = pq_tab_eval(tab, v);
        }
    }
}
