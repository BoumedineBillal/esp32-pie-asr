// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * transcript.hpp: tokens into words and lines. A word is final when the next word starts (a piece beginning with
 * U+2581) or after word_timeout without a token; a pause of `pause` or more before a word starts a new line. Only a
 * piece that starts a word starts a line, and the pause is counted from the last piece of a word (a late "." stays on
 * its line). The words, lines and texts live in PSRAM; when they fill up, the older half of the lines is dropped
 * (generation() changes), so a session can last any time.
 */
#include "transcript.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "transcript_internal.hpp"

namespace transcript {

/* about 20 minutes of speech before the older half goes */
static constexpr int MAX_WORDS = 3000, MAX_LINES = 1000, POOL = 32 * 1024, MAX_CUR = 96;
static constexpr int FRAME_MS = 40;

static Word *s_w;
static Line *s_l;
static char *s_pool;
static int32_t *s_sorted;
static int s_nw, s_nl;
static uint32_t s_used;
static SemaphoreHandle_t s_lock;
static std::atomic<uint32_t> s_version{0}, s_generation{0};
static uint32_t s_dropped_words, s_dropped_lines; /* by make_room since the last clear */
static const char *const *s_pieces;
static int s_npieces;
static int s_timeout = 12, s_pause = 38; /* frames: 0.5 s, 1.5 s */

/* the stream */
static int64_t s_frame, s_cur_end, s_last_token = -1000000000;
static int64_t s_last_line_tok = -1000000000; /* the last piece that was part of a word (the pause for a new line) */
static int s_cur[MAX_CUR], s_ncur;
static char s_tent[384];

void init()
{
    s_w = static_cast<Word *>(heap_caps_calloc(MAX_WORDS, sizeof(Word), MALLOC_CAP_SPIRAM));
    s_l = static_cast<Line *>(heap_caps_calloc(MAX_LINES, sizeof(Line), MALLOC_CAP_SPIRAM));
    s_pool = static_cast<char *>(heap_caps_calloc(1, POOL, MALLOC_CAP_SPIRAM));
    s_sorted = static_cast<int32_t *>(heap_caps_calloc(MAX_WORDS, sizeof(int32_t), MALLOC_CAP_SPIRAM));
    rewrite::init();
    s_lock = xSemaphoreCreateMutex();
    clear();
}

void lock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
void unlock() { xSemaphoreGive(s_lock); }

void clear()
{
    lock();
    s_nw = 0;
    s_dropped_words = 0;
    s_dropped_lines = 0;
    s_nl = 1;
    s_l[0] = {0, 0};
    s_used = 0;
    s_frame = 0;
    s_ncur = 0;
    rewrite::clear();
    s_cur_end = 0;
    s_last_token = s_last_line_tok = -1000000000;
    s_tent[0] = 0;
    s_generation++;
    s_version++;
    unlock();
}

void set_rules(float word_timeout_s, float pause_s)
{
    lock();
    s_timeout = static_cast<int>(lrintf(word_timeout_s / 0.04f));
    s_pause = static_cast<int>(lrintf(pause_s / 0.04f));
    unlock();
}

void set_vocabulary(const char *const *pieces, int n)
{
    s_pieces = pieces;
    s_npieces = n;
}

void set_rewrites(const Rewrite *r, int n)
{
    lock();
    rewrite::set(r, n);
    unlock();
}

static const char *piece(int k) { return k > 0 && k < s_npieces && s_pieces[k] ? s_pieces[k] : ""; }

/* the pieces joined, U+2581 (E2 96 81) as spaces, stripped */
static void text_of(const int *toks, int n, char *out, size_t cap)
{
    size_t o = 0;
    for (int i = 0; i < n; i++)
        for (const char *p = piece(toks[i]); *p && o + 1 < cap;) {
            if ((uint8_t)p[0] == 0xE2 && (uint8_t)p[1] == 0x96 && (uint8_t)p[2] == 0x81) {
                out[o++] = ' ';
                p += 3;
            } else {
                out[o++] = *p++;
            }
        }
    out[o] = 0;
    size_t a = 0;
    while (out[a] == ' ')
        a++;
    while (o > a && out[o - 1] == ' ')
        o--;
    memmove(out, out + a, o - a);
    out[o - a] = 0;
}

static bool starts_word(int k)
{
    const char *p = piece(k);
    return (uint8_t)p[0] == 0xE2 && (uint8_t)p[1] == 0x96 && (uint8_t)p[2] == 0x81;
}

/* the older half of the lines out, when the pools are nearly full */
static void make_room(size_t text_bytes)
{
    if (s_nw + 1 < MAX_WORDS && s_nl + 1 < MAX_LINES && s_used + text_bytes + 1 < POOL)
        return;
    int L = s_nl / 2 > 0 ? s_nl / 2 : 1;
    if (L >= s_nl)
        L = s_nl - 1;
    uint32_t W = s_l[L].first_word, P = W < (uint32_t)s_nw ? s_w[W].text : s_used;
    memmove(s_w, s_w + W, sizeof(Word) * (s_nw - W));
    s_nw -= static_cast<int>(W);
    for (int i = 0; i < s_nw; i++) {
        s_w[i].text -= P;
        s_w[i].line = static_cast<uint16_t>(s_w[i].line - L);
    }
    memmove(s_pool, s_pool + P, s_used - P);
    s_used -= P;
    memmove(s_l, s_l + L, sizeof(Line) * (s_nl - L));
    s_nl -= L;
    for (int i = 0; i < s_nl; i++)
        s_l[i].first_word -= W;
    s_dropped_words += W;
    s_dropped_lines += L;
    s_generation++;
}

void append_word(const char *txt, bool punct, int32_t end_ms, int32_t received_ms, bool cont)
{
    size_t n = strlen(txt);
    make_room(n);
    Word &w = s_w[s_nw];
    w.text = s_used;
    w.len = static_cast<uint16_t>(n);
    memcpy(s_pool + s_used, txt, n + 1);
    s_used += static_cast<uint32_t>(n + 1);
    w.space = !punct || s_nw == 0;
    w.cont = cont && s_nw > 0;
    w.line = static_cast<uint16_t>(s_nl - 1);
    w.end_ms = end_ms;
    /* (not before its own end: at the end of a stream the last word can end in the padding after the audio) */
    w.final_ms = std::max(received_ms, w.end_ms);
    w.shown_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    s_nw++;
}

/* the word being written is closed: final now, or waiting for a rewrite */
static void close_word(int32_t received_ms)
{
    if (!s_ncur)
        return;
    char txt[384];
    text_of(s_cur, s_ncur, txt, sizeof txt);
    const bool cont = !starts_word(s_cur[0]); /* (the rest of a word the time-out closed) */
    s_ncur = 0;
    if (!txt[0])
        return;
    bool punct = true;
    for (const char *p = txt; *p; p++)
        if ((uint8_t)*p >= 0x80 || (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))
            punct = false;
    const int32_t end_ms = static_cast<int32_t>((s_cur_end + 1) * FRAME_MS);
    if (rewrite::active())
        rewrite::push(txt, punct, end_ms, received_ms, cont && !punct);
    else
        append_word(txt, punct, end_ms, received_ms, cont && !punct);
}

/* the word being written: the waiting words, then the current one */
static void tentative_text()
{
    size_t o = rewrite::text(s_tent, sizeof s_tent, 0);
    char cur[384];
    text_of(s_cur, s_ncur, cur, sizeof cur);
    if (cur[0] && o)
        o = append(s_tent, sizeof s_tent, o, " ");
    append(s_tent, sizeof s_tent, o, cur);
}

void add_tokens(const int *tok, int n, int32_t received_ms)
{
    lock();
    for (int i = 0; i < n; i++) {
        int k = tok[i];
        int64_t f = s_frame;
        if (k) {
            const bool sw = starts_word(k);
            if (sw && s_ncur)
                close_word(received_ms);
            if (!s_ncur && sw && f - s_last_line_tok >= s_pause && (s_nw || rewrite::pending())) {
                rewrite::resolve(true, received_ms); /* (waiting words stay on the line they were spoken on) */
                make_room(0);
                s_l[s_nl++] = {static_cast<int32_t>(f * FRAME_MS), static_cast<uint32_t>(s_nw)};
            }
            if (sw || s_ncur)
                s_last_line_tok = f;
            if (s_ncur < MAX_CUR)
                s_cur[s_ncur++] = k;
            s_cur_end = s_last_token = f;
        } else if (s_ncur && f - s_last_token >= s_timeout) {
            close_word(received_ms);
        }
        if (rewrite::pending() && !s_ncur && f - s_last_token >= s_timeout)
            rewrite::resolve(true, received_ms); /* no next word in time: the waiting ones go out */
        s_frame++;
    }
    tentative_text();
    s_version++;
    unlock();
}

void finish(int32_t received_ms)
{
    lock();
    close_word(received_ms);
    rewrite::resolve(true, received_ms);
    s_tent[0] = 0;
    s_version++;
    unlock();
}

uint32_t version() { return s_version.load(); }
uint32_t generation() { return s_generation.load(); }
uint32_t dropped_words() { return s_dropped_words; }
uint32_t dropped_lines() { return s_dropped_lines; }
int words() { return s_nw; }
const Word &word(int i) { return s_w[i]; }
const char *text(const Word &w) { return s_pool + w.text; }
int lines() { return s_nl; }
const Line &line(int i) { return s_l[i]; }
const char *tentative() { return s_tent; }

Delays delays()
{
    Delays d = {s_nw, 0, 0, 0};
    if (!s_nw)
        return d;
    for (int i = 0; i < s_nw; i++)
        s_sorted[i] = s_w[i].final_ms - s_w[i].end_ms;
    d.last = s_sorted[s_nw - 1];
    std::sort(s_sorted, s_sorted + s_nw);
    d.median = s_sorted[s_nw / 2];
    d.p95 = s_sorted[std::min(s_nw - 1, s_nw * 95 / 100)];
    return d;
}

int delay_values(int32_t *out, int max)
{
    int n = std::min(max, s_nw);
    for (int i = 0; i < n; i++) {
        const Word &w = s_w[s_nw - n + i];
        out[i] = w.final_ms - w.end_ms;
    }
    return n;
}

} // namespace transcript
