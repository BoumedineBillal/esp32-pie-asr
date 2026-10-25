// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// Phrase biasing: the phrase file's items and the trie of their token sequences that the beam search follows.
#pragma once
#include <stddef.h>
#include "pieasr/model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PIEASR_PHRASES_MAX 32   // items of a phrase file
#define PIEASR_PHRASE_FORMS 4   // written forms per item
#define PIEASR_PHRASE_TEXT 96   // bytes per text, NUL included

// one item of the file: a line "shown" or "shown = spoken | spoken" ('#' starts a comment line)
typedef struct {
    char shown[PIEASR_PHRASE_TEXT];                       // as shown on the screen and in the transcript
    char forms[PIEASR_PHRASE_FORMS][PIEASR_PHRASE_TEXT];  // as the model writes it: forms[0] = shown, then the spoken
    int nforms;
    int word;                                             // a single word, else a sentence
} pieasr_phrase_t;

// the items of a phrase file (its text, NUL-terminated); returns how many (at most max), *bad: lines not understood
int pieasr_phrases_parse(const char *text, pieasr_phrase_t *items, int max, int *bad);

// The trie: a hypothesis that follows a phrase gets `bonus` per token, loses what it got if the phrase breaks off and
// keeps it once the phrase is complete (a longer phrase may still continue). No phrases: exactly the plain search.
typedef struct pieasr_trie pieasr_trie_t;

pieasr_trie_t *pieasr_trie_new(void *(*alloc)(size_t), void (*release)(void *));   // the root only; NULL: no memory
void pieasr_trie_clear(pieasr_trie_t *t);                                          // back to the root only
void pieasr_trie_free(pieasr_trie_t *t);
int pieasr_trie_add_tokens(pieasr_trie_t *t, const int *tok, int n, float bonus);  // one token sequence; 0 = ok
// one text as the model's pieces: each word split every way its pieces allow, the fewest-piece splits kept, the first
// word also capitalized; returns the sequences added, -1 if a word has no split
int pieasr_trie_add_text(pieasr_trie_t *t, const pieasr_model_t *m, const char *text, float bonus);
void pieasr_trie_build(pieasr_trie_t *t);   // after the last add
int pieasr_trie_nodes(const pieasr_trie_t *t);

#ifdef __cplusplus
}
#endif
