// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The joiner: tanh(encoder frame + decoder output), then the output layer to the vocabulary's logits.
#include <math.h>
#include "math/fastmath.h"
#include "model_internal.h"

float pq_tanh(float x)
{
    return pq_fm_tanh_tab(x);
}

void pq_joiner_input(const float *enc, const float *dec, float *u)
{
    for (int c = 0; c < PIEASR_DIM; c++) {
        u[c] = pq_tanh(enc[c] + dec[c]);
    }
}

void pq_joiner_logits(const pieasr_model_t *m, const float *u, int n, float *lg)
{
    pq_dense_apply(&m->out_lin_q, u, n, PIEASR_DIM, lg, PIEASR_VOCAB, PQ_DENSE_POST_NONE);
}

void pq_joiner_logits_float(const pieasr_model_t *m, const float *u, float *lg)
{
    for (int o = 0; o < PIEASR_VOCAB; o++) {   // output_linear stored [in 512, out 650]
        float a = 0.0f;
        for (int i = 0; i < PIEASR_DIM; i++) {
            a += pq_q8_at(m, m->out_lin, i * PIEASR_VOCAB + o) * u[i];
        }
        lg[o] = a + pq_h2f(m->out_lin_b, o);
    }
}

int pq_joiner_best(const float *lg)
{
    int best = 0;
    float bv = -INFINITY;
    for (int o = 0; o < PIEASR_VOCAB; o++) {
        if (lg[o] > bv) {
            bv = lg[o];
            best = o;
        }
    }
    return best;
}
