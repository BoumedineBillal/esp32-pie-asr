// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The activation tables of fastmath.h.
#include "math/fastmath.h"

float *pq_fm_tab_g, *pq_fm_tab_s, *pq_fm_tab_t, *pq_fm_tab_e;

int pq_fm_tables_init(void *(*alloc)(size_t))
{
    if (pq_fm_tab_g) {
        return 0;
    }
    float *t = alloc(sizeof(float) * 4 * (PQ_FM_TAB_N + 2));
    if (!t) {
        return -1;
    }
    pq_fm_tab_g = t;
    pq_fm_tab_s = t + (PQ_FM_TAB_N + 2);
    pq_fm_tab_t = t + 2 * (PQ_FM_TAB_N + 2);
    pq_fm_tab_e = t + 3 * (PQ_FM_TAB_N + 2);
    for (int i = 0; i < PQ_FM_TAB_N + 2; i++) {   // (one entry past 16: the interpolation at a = 16 reads i + 1)
        const float a = (float)i / PQ_FM_TAB_STEPS;
        pq_fm_tab_g[i] = pq_fm_softplus(-a);         // log1p(exp(-a))
        pq_fm_tab_s[i] = pq_fm_sigmoid(a) - 0.5f;
        pq_fm_tab_t[i] = pq_fm_tanh(a);
        pq_fm_tab_e[i] = pq_fm_exp(-a);
    }
    return 0;
}
