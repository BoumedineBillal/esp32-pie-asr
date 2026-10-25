// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The transcript: the committed tokens (one per 40 ms encoder frame, 0 = blank) made into words and lines. A word is
 * final when the next word starts (a token beginning with U+2581) or after word_timeout without a token; a pause of
 * `pause` or more before a word starts a new line. Each word keeps when it was spoken until (its last token's frame
 * end) and when it became final (the audio received by then), so its delay.
 *
 * Written by the recogniser, read by the screen and the PC: lock() / unlock() around reads; version() changes on any
 * change (a new word, the word being written, a new line), generation() when words were dropped or cleared.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace transcript {

struct Word {
    uint32_t text;       /* offset of its UTF-8 text in the pool (NUL-terminated) */
    uint16_t len;        /* bytes */
    uint16_t line;       /* its line (0, 1, ...) */
    int32_t end_ms;      /* spoken until (audio time) */
    int32_t final_ms;    /* final when this much audio had come in */
    uint32_t shown_ms;   /* the board's time then (esp_timer, ms: the screen fades its highlight) */
    bool space;          /* a space before it (not before punctuation) */
    bool cont;           /* it does not start with a word's first piece: the rest of the word before it, closed by the
                          * word time-out while the model was still on it ("s" "witch") */
};

struct Line {
    int32_t start_ms;    /* its first token (audio time) */
    uint32_t first_word; /* index of its first word */
};

void init();             /* the pools in PSRAM */
void clear();
/* the rules' parameters */
void set_rules(float word_timeout_s, float pause_s);
/* the token pieces (the model's vocabulary, index -> UTF-8 piece) */
void set_vocabulary(const char *const *pieces, int n);
/* n frames' tokens; received_ms: the audio received when they came */
void add_tokens(const int *tok, int n, int32_t received_ms);
/* the phrases' rewrites: words written as `spoken` (words separated by spaces; case and a word's trailing
 * punctuation ignored) become one word `shown` (the last word's punctuation kept). Words that may start a spoken form
 * wait (shown as the word being written) until the form completes, or breaks off, or word_timeout passes; then the
 * longest complete form is rewritten. n = 0: none (every word final as before). The strings must stay valid. */
struct Rewrite {
    const char *spoken, *shown;
};
void set_rewrites(const Rewrite *r, int n);
/* the end of the stream: the word being written becomes final */
void finish(int32_t received_ms);

void lock();
void unlock();
uint32_t version();
uint32_t generation();
/* the words and lines dropped to make room since the last clear (under lock()): word(i) is the session's word
 * dropped_words() + i, line l its line dropped_lines() + l */
uint32_t dropped_words();
uint32_t dropped_lines();
int words();
const Word &word(int i);
const char *text(const Word &w);
int lines();
const Line &line(int i);
/* the word being written (not final yet), "" if none */
const char *tentative();
/* delays of the final words (ms): count, median, p95, the last */
struct Delays {
    int n;
    int32_t median, p95, last;
};
Delays delays();
/* the words' delays, for a histogram: up to max values, newest last; returns how many */
int delay_values(int32_t *out, int max);

} // namespace transcript
