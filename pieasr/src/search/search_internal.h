// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The beam search's and the phrase trie's state, shared by the search's files (and the host tests).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "model_internal.h"
#include "pieasr/phrases.h"
#include "pieasr/search.h"

#define PQ_BEAM_TAIL 384   // open tokens per hypothesis (PQ_BEAM_LAG keeps it far below)
#define PQ_BEAM_LAG 75     // frames (3 s): an open token older than this is committed

// ---- the phrase trie
typedef struct {
    int token, parent;
    float bonus, score, base;   // score: the unfinished bonus on arriving here; base: while here (0 at a phrase end)
    int fail, child, sibling, end;
} pq_trie_node_t;

struct pieasr_trie {
    pq_trie_node_t *nodes;
    int n, cap;
    void *(*alloc)(size_t);
    void (*release)(void *);
};

// the state after token tok from state; *delta: the score change
int pq_trie_step(const pieasr_trie_t *t, int state, int tok, float *delta);
float pq_trie_final(const pieasr_trie_t *t, int state);   // the score change at the end of the stream

// ---- the search
typedef struct {
    int16_t tok[PQ_BEAM_TAIL];
    int32_t fr[PQ_BEAM_TAIL];   // the frame of each open token
    int n;
    int c0, c1;                 // its last two tokens (the decoder's context, committed ones included)
    float lp;                   // its score relative to the frame's best (phrase bonuses included)
    int cstate;
    float dec[PIEASR_DIM];      // the decoder's output for (c0, c1)
} pq_hyp_t;

struct pieasr_beam {
    int beam;
    const pieasr_trie_t *trie;  // NULL: no phrases
    pq_hyp_t *hyp, *tmp;
    int n;
    double offset;              // the common part of the scores
    int frame;                  // frames searched
    int ncommitted;
    int last_fr;                // the last committed token's frame (frames grow by at least one: the transcript counts
                                // them)
    int16_t out_tok[PIEASR_BEAM_OUT];
    int32_t out_fr[PIEASR_BEAM_OUT];
    int nout;
    float *u, *lg;              // per frame: the joiner's inputs and outputs
    uint32_t joiner_rows, decoder_calls, forced;   // work, and commits forced by PQ_BEAM_LAG
    int pause;                  // pieasr_beam_set_pause's frames (0: off)
};

// commit.c
void pq_beam_commit(pieasr_beam_t *b);           // after each frame
void pq_beam_pop_first(pieasr_beam_t *b, int fr);   // every hypothesis' first open token committed at frame fr
int pq_beam_best(const pieasr_beam_t *b);
