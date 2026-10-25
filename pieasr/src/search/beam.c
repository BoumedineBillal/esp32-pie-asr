// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The beam search: k2's modified_beam_search (at most one token per encoder frame; hypotheses with the same tokens
// merged by log-add; the best one length-normalized at the end), with the phrase bonuses.
#include <stdlib.h>
#include <string.h>
#include "math/detmath.h"
#include "math/fastmath.h"
#include "search/search_internal.h"

#define V PIEASR_VOCAB

// per hypothesis and frame, the tokens with an arc from its trie state (the rest go to the root): next node and delta
static int16_t *s_spn;   // [V], -1: no arc
static float *s_spd;     // [V]

pieasr_beam_t *pieasr_beam_new(int beam, void *(*alloc)(size_t))
{
    if (beam < 1 || beam > PIEASR_BEAM_MAX) {
        return NULL;
    }
    pieasr_beam_t *b = alloc(sizeof *b);
    if (!b) {
        return NULL;
    }
    memset(b, 0, sizeof *b);
    b->beam = beam;
    b->hyp = alloc(sizeof(pq_hyp_t) * (size_t)beam);
    b->tmp = alloc(sizeof(pq_hyp_t) * (size_t)beam);
    b->u = alloc(sizeof(float) * (size_t)beam * PIEASR_DIM);
    b->lg = alloc(sizeof(float) * (size_t)beam * V);
    if (!s_spn) {
        s_spn = alloc(sizeof(int16_t) * V);
        s_spd = alloc(sizeof(float) * V);
        if (s_spn) {
            for (int k = 0; k < V; k++) {
                s_spn[k] = -1;
            }
        }
    }
    return b->hyp && b->tmp && b->u && b->lg && s_spn && s_spd ? b : NULL;
}

void pieasr_beam_reset(pieasr_beam_t *b, pieasr_model_t *m)
{
    b->n = 1;
    pq_hyp_t *h = &b->hyp[0];
    h->n = 0;
    h->c0 = h->c1 = 0;
    h->lp = 0.0f;
    h->cstate = 0;
    pq_decoder_for(m, 0, 0, h->dec);
    b->decoder_calls++;
    b->offset = 0.0;
    b->frame = 0;
    b->ncommitted = 0;
    b->last_fr = -1;
    b->nout = 0;
}

void pieasr_beam_set_phrases(pieasr_beam_t *b, const pieasr_trie_t *t)
{
    b->trie = t;
    for (int h = 0; h < b->n; h++) {
        b->hyp[h].cstate = 0;
    }
}

void pieasr_beam_set_pause(pieasr_beam_t *b, int frames)
{
    b->pause = frames > 0 ? frames : 0;
}

typedef struct {
    float s;
    int h, k, cs;
} cand_t;

// keep the best B of the candidates in top (best first; a tie keeps the one already there)
static void insert(cand_t *top, int *nt, int B, cand_t c)
{
    if (*nt == B && !(c.s > top[B - 1].s)) {
        return;
    }
    int i = *nt < B ? (*nt)++ : B - 1;
    while (i > 0 && c.s > top[i - 1].s) {
        top[i] = top[i - 1];
        i--;
    }
    top[i] = c;
}

static void frame(pieasr_beam_t *b, pieasr_model_t *m, const float *e)
{
    const int H = b->n, B = b->beam;
    const pieasr_trie_t *g = b->trie && b->trie->n > 1 ? b->trie : NULL;
    for (int h = 0; h < H; h++) {
        pq_joiner_input(e, b->hyp[h].dec, b->u + (size_t)h * PIEASR_DIM);
    }
    // the joiner on every hypothesis in one product (its rows are independent: int8 inputs per row)
    pq_joiner_logits(m, b->u, H, b->lg);
    b->joiner_rows += (uint32_t)H;
    cand_t top[PIEASR_BEAM_MAX];
    int nt = 0;
    for (int h = 0; h < H; h++) {
        const pq_hyp_t *p = &b->hyp[h];
        const float *l = b->lg + (size_t)h * V;
        // the phrase arcs from this hypothesis' state (closest node first, as pq_trie_step follows them)
        int16_t marked[256];
        int nm = 0;
        float dflt = 0.0f;
        if (g) {
            dflt = -g->nodes[p->cstate].base;
            for (int u = p->cstate;; u = g->nodes[u].fail) {
                for (int c = g->nodes[u].child; c >= 0; c = g->nodes[c].sibling) {
                    const int t = g->nodes[c].token;
                    if (t > 0 && t < V && s_spn[t] < 0 && nm < 256) {
                        s_spn[t] = (int16_t)c;
                        s_spd[t] = g->nodes[c].score - g->nodes[p->cstate].base;
                        marked[nm++] = (int16_t)t;
                    }
                }
                if (u == 0) {
                    break;
                }
            }
        }
        // its own best B tokens by logit + phrase delta (exact within the hypothesis)
        cand_t loc[PIEASR_BEAM_MAX];
        int nl = 0;
        for (int k = 0; k < V; k++) {
            float d = 0.0f;
            int cs = p->cstate;
            if (k && g) {
                if (s_spn[k] >= 0) {
                    d = s_spd[k];
                    cs = s_spn[k];
                } else {
                    d = dflt;
                    cs = 0;
                }
            }
            insert(loc, &nl, B, (cand_t){l[k] + d, h, k, cs});
        }
        for (int i = 0; i < nm; i++) {
            s_spn[marked[i]] = -1;
        }
        // its log-softmax's normalizer, then its candidates' scores into the best B over all hypotheses
        float mx = l[0], sum = 0.0f;
        for (int k = 1; k < V; k++) {
            mx = l[k] > mx ? l[k] : mx;
        }
        for (int k = 0; k < V; k++) {
            sum += pq_fm_exp(l[k] - mx);
        }
        const float base = p->lp - (mx + dm_logf(sum));
        for (int i = 0; i < nl; i++) {
            cand_t c = loc[i];
            c.s = base + c.s;
            insert(top, &nt, B, c);
        }
    }
    // the new hypotheses; the same tokens reached twice (one by blank, one by its last token) are merged
    int nn = 0, fresh[PIEASR_BEAM_MAX];
    for (int i = 0; i < nt; i++) {
        const pq_hyp_t *p = &b->hyp[top[i].h];
        const int k = top[i].k;
        if (k && p->n == PQ_BEAM_TAIL) {
            continue;
        }
        const int n = p->n + (k != 0);
        int j = 0;
        for (; j < nn; j++) {
            const pq_hyp_t *q = &b->tmp[j];
            if (q->n == n && !memcmp(q->tok, p->tok, sizeof(int16_t) * (size_t)p->n) &&
                (!k || q->tok[n - 1] == k)) {
                break;
            }
        }
        if (j < nn) {
            const float hi = b->tmp[j].lp > top[i].s ? b->tmp[j].lp : top[i].s;
            const float lo = b->tmp[j].lp > top[i].s ? top[i].s : b->tmp[j].lp;
            b->tmp[j].lp = hi + dm_logf(1.0f + pq_fm_exp(lo - hi));
            continue;
        }
        pq_hyp_t *q = &b->tmp[nn];
        memcpy(q->tok, p->tok, sizeof(int16_t) * (size_t)p->n);
        memcpy(q->fr, p->fr, sizeof(int32_t) * (size_t)p->n);
        q->n = p->n;
        q->c0 = p->c0;
        q->c1 = p->c1;
        q->lp = top[i].s;
        q->cstate = top[i].cs;
        fresh[nn] = k != 0;
        if (k) {
            q->tok[q->n] = (int16_t)k;
            q->fr[q->n] = b->frame;
            q->n++;
            q->c0 = p->c1;
            q->c1 = k;
        } else {
            memcpy(q->dec, p->dec, sizeof q->dec);
        }
        nn++;
    }
    for (int j = 0; j < nn; j++) {
        if (fresh[j]) {
            pq_decoder_for(m, b->tmp[j].c0, b->tmp[j].c1, b->tmp[j].dec);
            b->decoder_calls++;
        }
    }
    pq_hyp_t *t = b->hyp;
    b->hyp = b->tmp;
    b->tmp = t;
    b->n = nn;
    // scores relative to the best (float keeps its precision over any length); the common part in offset
    const float top_lp = b->hyp[pq_beam_best(b)].lp;
    for (int h = 0; h < b->n; h++) {
        b->hyp[h].lp -= top_lp;
    }
    b->offset += top_lp;
    b->frame++;
    pq_beam_commit(b);
}

void pieasr_beam_chunk(pieasr_beam_t *b, pieasr_model_t *m, const float *enc, int T)
{
    for (int t = 0; t < T; t++) {
        frame(b, m, enc + (size_t)t * PIEASR_DIM);
    }
}

void pieasr_beam_finish(pieasr_beam_t *b)
{
    if (!b->n) {
        return;
    }
    // the best hypothesis, length-normalized (icefall's get_most_probable(length_norm=True): tokens + 2)
    int best = 0;
    double bs = 0.0;
    for (int h = 0; h < b->n; h++) {
        const pq_hyp_t *p = &b->hyp[h];
        const double s = ((double)p->lp + b->offset + (b->trie ? pq_trie_final(b->trie, p->cstate) : 0.0f)) /
                         (double)(b->ncommitted + p->n + 2);
        if (h == 0 || s > bs) {
            bs = s;
            best = h;
        }
    }
    if (best) {
        b->hyp[0] = b->hyp[best];
    }
    b->n = 1;
    while (b->hyp[0].n) {
        pq_beam_pop_first(b, b->hyp[0].fr[0]);
    }
}
