// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The beam search's commits on hand-made hypotheses: the forced commit of an old open token, the tokens all
// hypotheses agree on, the settled frames.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "search/search_internal.h"

static int fails;

static void check(const char *what, double got, double want)
{
    const int ok = fabs(got - want) < 1e-5;
    fails += !ok;
    printf("%s %-70s %+.2f (want %+.2f)\n", ok ? "PASS" : "FAIL", what, got, want);
}

static void *zalloc(size_t n)
{
    return calloc(1, n);
}

int main(void)
{
    pieasr_beam_t *b = pieasr_beam_new(4, zalloc);
    if (!b) {
        printf("FAIL: no memory\n");
        return 1;
    }
    check("beam 0 is refused", pieasr_beam_new(0, zalloc) == NULL, 1);
    check("beam PIEASR_BEAM_MAX + 1 is refused", pieasr_beam_new(PIEASR_BEAM_MAX + 1, zalloc) == NULL, 1);
    b->last_fr = -1;
    // the best hypothesis has no open token, a weaker one a stray token 100 frames old: the weaker one goes and every
    // frame is settled
    b->n = 2;
    b->hyp[0].n = 0;
    b->hyp[0].lp = 0.0f;
    b->hyp[1].n = 1;
    b->hyp[1].tok[0] = 5;
    b->hyp[1].fr[0] = 0;
    b->hyp[1].lp = -1.0f;
    b->frame = 100;
    pq_beam_commit(b);
    check("best without an open token, a stray one 100 frames old: hypotheses", b->n, 1);
    check("  ... settled up to the present frame", pieasr_beam_settled(b), 100);
    check("  ... nothing committed", b->nout, 0);
    check("  ... the frames searched", pieasr_beam_frame(b), 100);
    // the best has an old open token: committed, the one without it dropped
    b->n = 2;
    b->nout = 0;
    b->hyp[0].n = 1;
    b->hyp[0].tok[0] = 7;
    b->hyp[0].fr[0] = 10;
    b->hyp[0].lp = 0.0f;
    b->hyp[1].n = 0;
    b->hyp[1].lp = -0.5f;
    pq_beam_commit(b);
    check("best with a token 90 frames old: committed", b->nout, 1);
    check("  ... the token", b->out_tok[0], 7);
    check("  ... the hypothesis without it dropped", b->n, 1);
    // tokens all agree on: committed at once, the frame from the best
    b->n = 2;
    b->nout = 0;
    b->frame = 20;
    b->last_fr = -1;
    b->hyp[0].n = 2;
    b->hyp[0].tok[0] = 9;
    b->hyp[0].fr[0] = 12;
    b->hyp[0].tok[1] = 4;
    b->hyp[0].fr[1] = 15;
    b->hyp[0].lp = 0.0f;
    b->hyp[1].n = 1;
    b->hyp[1].tok[0] = 9;
    b->hyp[1].fr[0] = 11;
    b->hyp[1].lp = -0.2f;
    pq_beam_commit(b);
    check("a token both hypotheses have: committed", b->nout, 1);
    check("  ... at the best one's frame", b->out_fr[0], 12);
    check("  ... the other open token stays (recent)", b->hyp[0].n, 1);
    check("  ... settled before it", pieasr_beam_settled(b), 15);
    // taking the committed tokens
    int16_t tok[4];
    int32_t fr[4];
    check("take before frame 12: none", pieasr_beam_take(b, 12, tok, fr, 4), 0);
    check("take before frame 13: one", pieasr_beam_take(b, 13, tok, fr, 4), 1);
    check("  ... its token", tok[0], 9);
    check("  ... the queue is empty", b->nout, 0);
    printf("%s: %d failed\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
