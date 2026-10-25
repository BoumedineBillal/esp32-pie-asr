// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The greedy search: per encoder frame the joiner's best token; the decoder is updated after each emitted token.
#include <stddef.h>
#include "model_internal.h"

int pieasr_greedy_win = 6;

int pieasr_greedy_step(pieasr_model_t *m, const float *enc_frame)
{
    float u[PIEASR_DIM], logits[PIEASR_VOCAB];
    pq_joiner_input(enc_frame, m->dec, u);
    if (pieasr_opt_q8 == 2 && m->out_lin_q.w) {
        pq_joiner_logits(m, u, 1, logits);
    } else {
        pq_joiner_logits_float(m, u, logits);
    }
    const int best = pq_joiner_best(logits);
    if (best != 0) {
        m->ctx[0] = m->ctx[1];
        m->ctx[1] = best;
        pq_update_decoder(m);
    }
    return best;
}

// The same decisions as pieasr_greedy_step frame by frame. On the int8 path the joiner runs on a window of frames in
// one product (its matrix read once per window instead of once per frame): the decoder state only changes when a
// token is emitted, and then the frames after it are computed again.
void pieasr_greedy_chunk(pieasr_model_t *m, const float *enc, int T, int *tok)
{
    if (!(pieasr_opt_q8 == 2 && m->out_lin_q.w)) {
        for (int t = 0; t < T; t++) {
            tok[t] = pieasr_greedy_step(m, enc + (size_t)t * PIEASR_DIM);
        }
        return;
    }
    m->ws_used = 0;
    float *u = pq_ws_alloc(m, (size_t)T * PIEASR_DIM), *logits = pq_ws_alloc(m, (size_t)T * PIEASR_VOCAB);
    int t = 0;
    while (t < T) {
        // the window (the tokens do not depend on it: each frame's logits use the current decoder state): fewer frames
        // evaluated in vain after a token, at the cost of more weight reads
        const int n = T - t < pieasr_greedy_win ? T - t : pieasr_greedy_win;
        for (int f = 0; f < n; f++) {
            pq_joiner_input(enc + (size_t)(t + f) * PIEASR_DIM, m->dec, u + (size_t)f * PIEASR_DIM);
        }
        PQ_TIMED(g_join, pq_joiner_logits(m, u, n, logits));
        int f = 0;
        for (; f < n; f++) {
            const int best = pq_joiner_best(logits + (size_t)f * PIEASR_VOCAB);
            tok[t + f] = best;
            if (best != 0) {
                m->ctx[0] = m->ctx[1];
                m->ctx[1] = best;
                PQ_TIMED(g_dec, pq_update_decoder(m));
                pieasr_prof.g_tokens++;
                break;
            }
        }
        t += f < n ? f + 1 : n;   // after the token, or the whole window when it had none
    }
    m->ws_used = 0;
}
