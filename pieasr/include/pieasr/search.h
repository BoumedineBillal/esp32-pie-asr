// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The beam search for a live stream, with phrase biasing. The same C on the host and the chip (float scores with
// deterministic exp and log), so both give the same tokens.
//
// k2's modified_beam_search: at most one token per encoder frame; hypotheses with the same tokens merged by log-add;
// the best one length-normalized at the end. Each frame: the joiner on every hypothesis in one product; each
// hypothesis' best `beam` tokens by its own logits (exact within a hypothesis: one hypothesis = greedy, bit for bit);
// then the best `beam` of those across hypotheses.
//
// The commits: tokens all hypotheses agree on are committed; an open token older than 75 frames (3 s) is committed
// from the best hypothesis and the hypotheses that disagree with it are dropped. pieasr_beam_settled(): the frames
// before it are final (no hypothesis has an open token there).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "pieasr/model.h"
#include "pieasr/phrases.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PIEASR_BEAM_MAX 8     // hypotheses
#define PIEASR_BEAM_OUT 512   // committed tokens not taken yet

typedef struct pieasr_beam pieasr_beam_t;

// a search of `beam` hypotheses (1..PIEASR_BEAM_MAX), its memory from alloc; NULL: no memory or a wrong beam
pieasr_beam_t *pieasr_beam_new(int beam, void *(*alloc)(size_t));
void pieasr_beam_reset(pieasr_beam_t *b, pieasr_model_t *m);   // a new stream (after pieasr_reset)
// the phrases from now (the hypotheses' trie states restart); NULL: none
void pieasr_beam_set_phrases(pieasr_beam_t *b, const pieasr_trie_t *t);
// a pause commits, from now: once the best hypothesis has had no token for `frames` frames, it is committed whole and
// the others go (the settled frames then reach the frames searched: the words before the pause are final without
// waiting for the hypotheses to agree or for the 75 frames); 0 (the default): never
void pieasr_beam_set_pause(pieasr_beam_t *b, int frames);
void pieasr_beam_chunk(pieasr_beam_t *b, pieasr_model_t *m, const float *enc, int T);   // T encoder frames
void pieasr_beam_finish(pieasr_beam_t *b);   // the end of the stream: the best hypothesis committed whole
int pieasr_beam_settled(const pieasr_beam_t *b);
int pieasr_beam_frame(const pieasr_beam_t *b);   // frames searched
// committed tokens with a frame before `upto`, in order (removed from the queue); returns how many
int pieasr_beam_take(pieasr_beam_t *b, int upto, int16_t *tok, int32_t *fr, int max);

#ifdef __cplusplus
}
#endif
