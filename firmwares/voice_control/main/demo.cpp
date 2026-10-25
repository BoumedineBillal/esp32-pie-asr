// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * demo.hpp. The transcript is read under its lock and copied; the rules and the simulation run on the copies.
 */
#include "demo.hpp"
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include "asr_service.hpp"
#include "freertos/FreeRTOS.h"
#include "transcript.hpp"

namespace demo {

static constexpr int LONGEST = 40;               /* words: an utterance this long is evaluated without a pause */

static Utterance s_last;
static vacuum::Sim s_vacuum;
static smarthome::Home s_home;
static car::Car s_car;
static factory::Line s_line;
static kitchen::Kitchen s_kitchen;
static presenter::Deck s_deck;
static voice::World *const WORLDS[COUNT] = {&s_vacuum, &s_home, &s_car, &s_line, &s_kitchen, &s_deck};
static int s_sel;
static voice::Context s_ctx;
static uint32_t s_next;                          /* the next word to take (index in the session: dropped + i) */
static uint32_t s_gen = 0xffffffff, s_dw;
static bool s_open;
static std::atomic<uint32_t> s_count{0};

/* the words waiting (final, not evaluated yet) and the one being written, for the screen */
static char s_wait[voice::MAX_WORDS][WORD_BYTES];
static int s_nwait;
static char s_tent[WORD_BYTES * 2];

/* say(): from any task to the UI task */
static portMUX_TYPE s_say_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_say[512];
static bool s_say_new;

const voice::Demo &rules() { return demos::get(s_sel); }
voice::World &world() { return *WORLDS[s_sel]; }
vacuum::Sim &vacuum() { return s_vacuum; }
smarthome::Home &home() { return s_home; }
car::Car &car() { return s_car; }
factory::Line &line() { return s_line; }
kitchen::Kitchen &kitchen() { return s_kitchen; }
presenter::Deck &deck() { return s_deck; }
int selected() { return s_sel; }

void select(int i)
{
    if (i < 0 || i >= COUNT || i == s_sel)
        return;
    s_sel = i;
    s_last = Utterance(); /* (an utterance is the demo's: its actions index its rules) */
    if (s_open)           /* from one demo's page to another's: words from now on, a fresh context */
        enter();
}

const Utterance &last() { return s_last; }
uint32_t utterances() { return s_count.load(); }
const char *being_written() { return s_tent; }

int waiting(const char **out, int max)
{
    const int n = s_nwait < max ? s_nwait : max;
    for (int i = 0; i < n; i++)
        out[i] = s_wait[i];
    return n;
}

static void copy_word(char *dst, const char *src) /* cut at WORD_BYTES - 1 */
{
    int i = 0;
    for (; src && src[i] && i < WORD_BYTES - 1; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

static void join_word(char *dst, const char *src) /* src after dst, cut at WORD_BYTES - 1 */
{
    int i = strlen(dst);
    for (; src && *src && i < WORD_BYTES - 1; src++)
        dst[i++] = *src;
    dst[i] = 0;
}

static bool has_letters(const char *s) /* not only punctuation */
{
    for (; *s; s++)
        if ((uint8_t)*s >= 0x80 || isalnum((uint8_t)*s))
            return true;
    return false;
}

/* the words of an utterance through the rules into the simulation */
static void evaluate(char (*words)[WORD_BYTES], int n, int32_t delay_ms)
{
    Utterance &u = s_last;
    u.n = n < voice::MAX_WORDS ? n : voice::MAX_WORDS;
    const char *w[voice::MAX_WORDS];
    for (int i = 0; i < u.n; i++) {
        if (words != u.words)
            copy_word(u.words[i], words[i]);
        w[i] = u.words[i];
        u.cancelled[i] = false;
    }
    voice::parse(rules(), w, u.n, u.r, &s_ctx);
    for (int i = 0; i < u.r.n; i++) { /* a cancelled action's words drawn struck through */
        const voice::Action &a = u.r.actions[i];
        if (!a.skipped)
            continue;
        for (int k = 0; k < a.ntrig; k++)
            if (a.trig[k].role != voice::VERB)
                for (int j = a.trig[k].first; j < a.trig[k].first + a.trig[k].count && j < u.n; j++)
                    u.cancelled[j] = true;
    }
    u.delay_ms = delay_ms;
    u.number++;
    world().order(u.r);
    s_count++;
}

/* an utterance that did nothing (no verb, no target, none of the demo's words) is kept a moment: the model can take
 * longer than the word time-out on a long word ("Stop" ... "everything" 0.56 s later), and the words that follow
 * within MERGE_MS are taken with it */
static constexpr int32_t MERGE_MS = 2000;
static constexpr int MERGE_WORDS = 8;
static char s_pre[MERGE_WORDS][WORD_BYTES];
static int s_npre;
static int32_t s_pre_end;

/* whether words would give an action (a trial: the context is a copy, the world untouched) */
static bool does(char (*words)[WORD_BYTES], int n)
{
    static voice::Result r;
    voice::Context ctx = s_ctx;
    const char *w[voice::MAX_WORDS];
    for (int i = 0; i < n && i < voice::MAX_WORDS; i++)
        w[i] = words[i];
    voice::parse(rules(), w, n < voice::MAX_WORDS ? n : voice::MAX_WORDS, r, &ctx);
    return r.n > 0;
}

static void keep_fragment(char (*words)[WORD_BYTES], int n, int32_t end_ms)
{
    const voice::Reason why = s_last.r.reason;
    const bool fragment = s_last.r.n == 0 && n <= MERGE_WORDS &&
                          (why == voice::NO_WORDS || why == voice::NO_VERB || why == voice::NO_TARGET ||
                           why == voice::NO_NUMBER);
    s_npre = 0;
    if (!fragment)
        return;
    for (int i = 0; i < n; i++)
        copy_word(s_pre[i], words[i]);
    s_npre = n;
    s_pre_end = end_ms;
}

void enter()
{
    transcript::lock();
    s_dw = transcript::dropped_words();
    s_next = s_dw + transcript::words();
    s_gen = transcript::generation();
    transcript::unlock();
    s_ctx = voice::Context();
    s_nwait = 0;
    s_tent[0] = 0;
    s_npre = 0;
    s_open = true;
}

void leave() { s_open = false; }

void reset()
{
    world().reset();
    s_last = Utterance();
    s_ctx = voice::Context();
    s_nwait = 0;
    s_tent[0] = 0;
    enter();
}

void say(const char *text)
{
    portENTER_CRITICAL(&s_say_lock);
    snprintf(s_say, sizeof s_say, "%s", text ? text : "");
    s_say_new = true;
    portEXIT_CRITICAL(&s_say_lock);
}

bool tick()
{
    bool changed = false;
    /* a say() first */
    char said[sizeof s_say];
    bool have_say = false;
    portENTER_CRITICAL(&s_say_lock);
    if (s_say_new) {
        memcpy(said, s_say, sizeof said);
        s_say_new = false;
        have_say = true;
    }
    portEXIT_CRITICAL(&s_say_lock);
    if (have_say) {
        static char words[voice::MAX_WORDS][WORD_BYTES];
        int n = 0;
        for (char *p = strtok(said, " "); p && n < voice::MAX_WORDS; p = strtok(nullptr, " "))
            copy_word(words[n++], p);
        s_npre = 0; /* (a tapped order is whole) */
        evaluate(words, n, -1);
        changed = true;
    }
    if (!s_open)
        return changed;
    /* the transcript's new final words, after a fragment kept from just before (see keep_fragment) */
    static char words[voice::MAX_WORDS][WORD_BYTES];
    int n = 0;
    int32_t end_ms = 0, first_end = -1;
    bool ready = false;
    char tent[sizeof s_tent];
    transcript::lock();
    const uint32_t gen = transcript::generation(), dw = transcript::dropped_words();
    const int nw = transcript::words();
    if (gen != s_gen) { /* words dropped (dropped_words grew), or cleared: then every word there is new */
        s_gen = gen;
        if (dw <= s_dw) { /* another stream: its times start again, and nothing it says continues the last one's */
            s_next = 0;
            s_npre = 0;
            s_ctx = voice::Context();
        }
        if (s_next > dw + nw)
            s_next = dw + nw;
    }
    s_dw = dw;
    int i0 = static_cast<int>(s_next) - static_cast<int>(dw);
    if (i0 < 0)
        i0 = 0;
    if (i0 < nw) { /* the fragment, if the new words came soon after it */
        for (int i = i0; i < nw && first_end < 0; i++)
            if (has_letters(transcript::text(transcript::word(i))))
                first_end = transcript::word(i).end_ms;
        if (s_npre && first_end >= 0 && first_end - s_pre_end < MERGE_MS)
            for (int k = 0; k < s_npre; k++)
                copy_word(words[n++], s_pre[k]);
    }
    const int fresh = n;
    for (int i = i0; i < nw && n < voice::MAX_WORDS; i++) {
        const transcript::Word &tw = transcript::word(i);
        /* punctuation, and the rest of a word the time-out cut ("s" "witch"): on the word before it */
        if ((!tw.space || tw.cont) && n > 0)
            join_word(words[n - 1], transcript::text(tw));
        else
            copy_word(words[n++], transcript::text(tw));
        if (has_letters(transcript::text(tw))) /* the speech's end (a "." can come out later) */
            end_ms = tw.end_ms;
    }
    snprintf(tent, sizeof tent, "%s", transcript::tentative());
    if (n > 0 && (!tent[0] || n >= LONGEST)) {
        ready = true;
        s_next = dw + nw;
    }
    transcript::unlock();
    /* only punctuation (a "." final after the word before it was taken): no utterance of its own */
    bool letters = false;
    for (int i = fresh; i < n; i++)
        letters = letters || has_letters(words[i]);
    if (!letters)
        n = 0;
    /* the words waiting, for the screen (cleared when they make an utterance) */
    const int nwait = ready ? 0 : n;
    if (nwait != s_nwait || strcmp(tent, s_tent)) {
        for (int i = 0; i < nwait; i++)
            copy_word(s_wait[i], words[i]);
        s_nwait = nwait;
        snprintf(s_tent, sizeof s_tent, "%s", ready ? "" : tent);
        changed = true;
    }
    if (ready && n > 0) {
        /* with the fragment, unless only the new words alone do something ("um" before "clean the kitchen") */
        const int from = fresh > 0 && !does(words, n) && does(words + fresh, n - fresh) ? fresh : 0;
        const asr::Stats st = asr::stats();
        evaluate(words + from, n - from, st.listening_ms > end_ms ? st.listening_ms - end_ms : 0);
        keep_fragment(words + from, n - from, end_ms);
        changed = true;
    }
    return changed;
}

} // namespace demo
