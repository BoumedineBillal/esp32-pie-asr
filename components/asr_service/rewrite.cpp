// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The phrases' rewrites: words the model writes as a spoken form ("ESP thirty two") become their shown form ("ESP32").
 * Words that may start a spoken form wait until it completes, breaks off, or the word time-out passes; then the
 * longest complete form becomes one word, with the last word's trailing punctuation.
 */
#include <cstdio>
#include <cstring>
#include "esp_heap_caps.h"
#include "transcript_internal.hpp"

namespace rewrite {

/* each spoken form as its words, normalized (lower case, no trailing punctuation) */
static constexpr int MAX_RW = 64, RW_WORDS = 8, RW_WORD = 24, MAX_PEND = 8;
struct Form {
    char w[RW_WORDS][RW_WORD];
    int n;
    const char *shown;
};
struct Pending {
    char text[384], norm[RW_WORD];
    bool punct, cont;
    int32_t end_ms;
};
static Form *s_rw;      /* PSRAM */
static int s_nrw;
static Pending *s_pend; /* PSRAM */
static int s_npend;

static bool is_tail_punct(char c) { return c == '.' || c == ',' || c == '?' || c == '!' || c == ';' || c == ':'; }

static void normalize(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (; in[n] && n + 1 < cap; n++)
        out[n] = (in[n] >= 'A' && in[n] <= 'Z') ? static_cast<char>(in[n] - 'A' + 'a') : in[n];
    while (n > 0 && is_tail_punct(out[n - 1]))
        n--;
    out[n] = 0;
}

void init()
{
    s_rw = static_cast<Form *>(heap_caps_calloc(MAX_RW, sizeof(Form), MALLOC_CAP_SPIRAM));
    s_pend = static_cast<Pending *>(heap_caps_calloc(MAX_PEND, sizeof(Pending), MALLOC_CAP_SPIRAM));
}

void set(const transcript::Rewrite *r, int n)
{
    s_nrw = 0;
    for (int i = 0; i < n && s_rw && s_nrw < MAX_RW; i++) {
        Form &f = s_rw[s_nrw];
        f.n = 0;
        f.shown = r[i].shown;
        char buf[128];
        snprintf(buf, sizeof buf, "%s", r[i].spoken);
        char *save = nullptr;
        for (char *t = strtok_r(buf, " ", &save); t && f.n < RW_WORDS; t = strtok_r(nullptr, " ", &save))
            normalize(t, f.w[f.n++], RW_WORD);
        if (f.n)
            s_nrw++;
    }
}

bool active() { return s_nrw > 0 && s_pend; }
int pending() { return s_npend; }
void clear() { s_npend = 0; }

/* the waiting words [0, k) are form f's first k words */
static bool starts(const Form &f, int k)
{
    for (int i = 0; i < k; i++)
        if (strcmp(f.w[i], s_pend[i].norm))
            return false;
    return true;
}

static void drop(int k)
{
    memmove(s_pend, s_pend + k, sizeof(Pending) * static_cast<size_t>(s_npend - k));
    s_npend -= k;
}

void resolve(bool force, int32_t received_ms)
{
    while (s_npend) {
        bool longer = false;
        int len = 0;
        const char *shown = nullptr;
        for (int i = 0; i < s_nrw; i++) {
            const Form &f = s_rw[i];
            if (f.n > s_npend) {
                longer = longer || starts(f, s_npend);
            } else if (f.n > len && starts(f, f.n)) {
                len = f.n;
                shown = f.shown;
            }
        }
        if (!force && longer)
            return;
        if (len) {
            const char *last = s_pend[len - 1].text;
            size_t p = strlen(last);
            while (p > 0 && is_tail_punct(last[p - 1]))
                p--;
            char txt[384];
            transcript::append(txt, sizeof txt, transcript::append(txt, sizeof txt, 0, shown), last + p);
            transcript::append_word(txt, false, s_pend[len - 1].end_ms, received_ms, s_pend[0].cont);
            drop(len);
        } else {
            transcript::append_word(s_pend[0].text, s_pend[0].punct, s_pend[0].end_ms, received_ms, s_pend[0].cont);
            drop(1);
        }
    }
}

void push(const char *txt, bool punct, int32_t end_ms, int32_t received_ms, bool cont)
{
    if (s_npend == MAX_PEND)
        resolve(true, received_ms);
    Pending &pw = s_pend[s_npend++];
    snprintf(pw.text, sizeof pw.text, "%s", txt);
    normalize(txt, pw.norm, sizeof pw.norm);
    pw.punct = punct;
    pw.cont = cont;
    pw.end_ms = end_ms;
    resolve(false, received_ms);
}

size_t text(char *dst, size_t cap, size_t o)
{
    for (int i = 0; i < s_npend; i++) {
        if (o && !s_pend[i].punct)
            o = transcript::append(dst, cap, o, " ");
        o = transcript::append(dst, cap, o, s_pend[i].text);
    }
    return o;
}

} // namespace rewrite
