// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The live stream's commits: the tokens every hypothesis agrees on, the forced commit of an open token older than
// PQ_BEAM_LAG frames, and (pieasr_beam_set_pause) the best hypothesis' at a pause; the committed tokens queue.
#include <string.h>
#include "search/search_internal.h"

static void push_out(pieasr_beam_t *b, int tok, int fr)
{
    if (b->nout == PIEASR_BEAM_OUT) {   // never with a consumer that takes every chunk; the oldest goes
        memmove(b->out_tok, b->out_tok + 1, sizeof(int16_t) * (PIEASR_BEAM_OUT - 1));
        memmove(b->out_fr, b->out_fr + 1, sizeof(int32_t) * (PIEASR_BEAM_OUT - 1));
        b->nout--;
    }
    b->out_tok[b->nout] = (int16_t)tok;
    b->out_fr[b->nout] = fr;
    b->nout++;
    b->ncommitted++;
}

// the first open token of every hypothesis committed (they all have it)
void pq_beam_pop_first(pieasr_beam_t *b, int fr)
{
    const int tok = b->hyp[0].tok[0];
    if (fr <= b->last_fr) {
        fr = b->last_fr + 1;
    }
    b->last_fr = fr;
    push_out(b, tok, fr);
    for (int h = 0; h < b->n; h++) {
        pq_hyp_t *p = &b->hyp[h];
        memmove(p->tok, p->tok + 1, sizeof(int16_t) * (size_t)(p->n - 1));
        memmove(p->fr, p->fr + 1, sizeof(int32_t) * (size_t)(p->n - 1));
        p->n--;
    }
}

int pq_beam_best(const pieasr_beam_t *b)
{
    int best = 0;
    for (int h = 1; h < b->n; h++) {
        if (b->hyp[h].lp > b->hyp[best].lp) {
            best = h;
        }
    }
    return best;
}

void pq_beam_commit(pieasr_beam_t *b)
{
    // an open token older than PQ_BEAM_LAG in any hypothesis: the best one decides; the hypotheses that disagree with
    // its first open token (or with its having none: a weaker one's stray token would hold the settled frames
    // forever) go, then its token, if any, is committed
    for (;;) {
        int oldest = b->frame;
        for (int h = 0; h < b->n; h++) {
            if (b->hyp[h].n && b->hyp[h].fr[0] < oldest) {
                oldest = b->hyp[h].fr[0];
            }
        }
        if (oldest >= b->frame - PQ_BEAM_LAG) {
            break;
        }
        const int best = pq_beam_best(b);
        const int has = b->hyp[best].n > 0, tok = has ? b->hyp[best].tok[0] : -1;
        const int fr = has ? b->hyp[best].fr[0] : 0;
        int w = 0;
        for (int h = 0; h < b->n; h++) {
            const pq_hyp_t *q = &b->hyp[h];
            if (has ? q->n && q->tok[0] == tok : !q->n) {
                if (w != h) {
                    b->hyp[w] = b->hyp[h];
                }
                w++;
            }
        }
        b->n = w;
        b->forced++;
        if (has) {
            pq_beam_pop_first(b, fr);
        }
    }
    for (;;) {   // the tokens they all agree on
        if (!b->n || !b->hyp[0].n) {
            break;
        }
        const int tok = b->hyp[0].tok[0];
        int all = 1;
        for (int h = 1; h < b->n && all; h++) {
            all = b->hyp[h].n && b->hyp[h].tok[0] == tok;
        }
        if (!all) {
            break;
        }
        pq_beam_pop_first(b, b->hyp[pq_beam_best(b)].fr[0]);
    }
    // a pause (pieasr_beam_set_pause): open tokens, and the best hypothesis without a token for b->pause frames (its
    // own pause: weaker ones keep trying tokens in a silence, and a case twin, "C" for "c", keeps them from ever
    // agreeing): it is committed whole, the others go
    if (b->pause && b->n) {
        int open = 0;
        for (int h = 0; h < b->n; h++) {
            open |= b->hyp[h].n > 0;
        }
        const int best = pq_beam_best(b);
        const pq_hyp_t *p = &b->hyp[best];
        const int last = p->n ? p->fr[p->n - 1] : b->last_fr;
        if (open && b->frame - 1 - last >= b->pause) {
            if (best) {
                b->hyp[0] = b->hyp[best];
            }
            b->n = 1;
            while (b->hyp[0].n) {
                pq_beam_pop_first(b, b->hyp[0].fr[0]);
            }
        }
    }
}

int pieasr_beam_settled(const pieasr_beam_t *b)
{
    int s = b->frame;
    for (int h = 0; h < b->n; h++) {
        if (b->hyp[h].n && b->hyp[h].fr[0] < s) {
            s = b->hyp[h].fr[0];
        }
    }
    return s;
}

int pieasr_beam_frame(const pieasr_beam_t *b)
{
    return b->frame;
}

int pieasr_beam_take(pieasr_beam_t *b, int upto, int16_t *tok, int32_t *fr, int max)
{
    int k = 0;
    while (k < b->nout && k < max && b->out_fr[k] < upto) {
        tok[k] = b->out_tok[k];
        fr[k] = b->out_fr[k];
        k++;
    }
    memmove(b->out_tok, b->out_tok + k, sizeof(int16_t) * (size_t)(b->nout - k));
    memmove(b->out_fr, b->out_fr + k, sizeof(int32_t) * (size_t)(b->nout - k));
    b->nout -= k;
    return k;
}
