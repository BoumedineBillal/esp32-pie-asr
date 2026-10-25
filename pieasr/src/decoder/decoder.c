// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The decoder: the embeddings of the last two tokens, the context convolution, ReLU, decoder_proj.
#include <string.h>
#include "model_internal.h"

void pq_update_decoder(pieasr_model_t *m)
{
    float e0[PIEASR_DIM], e1[PIEASR_DIM], d[PIEASR_DIM];
    for (int c = 0; c < PIEASR_DIM; c++) {
        e0[c] = pq_q8_at(m, m->emb, m->ctx[0] * PIEASR_DIM + c);
        e1[c] = pq_q8_at(m, m->emb, m->ctx[1] * PIEASR_DIM + c);
    }
    for (int o = 0; o < PIEASR_DIM; o++) {
        float a = 0.0f;
        for (int i = 0; i < 4; i++) {
            int c = 4 * (o / 4) + i;
            a += pq_h2f(m->dec_conv_w, (o * 4 + i) * 2 + 0) * e0[c] +
                 pq_h2f(m->dec_conv_w, (o * 4 + i) * 2 + 1) * e1[c];
        }
        d[o] = a > 0.0f ? a : 0.0f;
    }
    if (pieasr_opt_q8 == 2 && m->dec_proj_q.w) {   // the released int8 matrix on the tile kernel (int8 input per call)
        pq_dense_apply(&m->dec_proj_q, d, 1, PIEASR_DIM, m->dec, PIEASR_DIM, PQ_DENSE_POST_NONE);
        return;
    }
    // decoder_proj stored [in 512, out 512]
    for (int o = 0; o < PIEASR_DIM; o++) {
        float a = 0.0f;
        for (int i = 0; i < PIEASR_DIM; i++) {
            a += pq_q8_at(m, m->dec_proj, i * PIEASR_DIM + o) * d[i];
        }
        m->dec[o] = a + pq_h2f(m->dec_proj_b, o);
    }
}

void pq_decoder_for(pieasr_model_t *m, int c0, int c1, float *dec)
{
    const int s0 = m->ctx[0], s1 = m->ctx[1];
    float keep[PIEASR_DIM];
    memcpy(keep, m->dec, sizeof keep);
    m->ctx[0] = c0;
    m->ctx[1] = c1;
    pq_update_decoder(m);
    memcpy(dec, m->dec, sizeof keep);
    memcpy(m->dec, keep, sizeof keep);
    m->ctx[0] = s0;
    m->ctx[1] = s1;
}
