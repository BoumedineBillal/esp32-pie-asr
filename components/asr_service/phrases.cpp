// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * phrases.hpp: the phrase files' items, the switches, and the trie and the rewrites built from the items that are on.
 * Every list is parsed once at boot into its own memory, which stays: selecting a list swaps an index, so the screen
 * always reads valid items while the main task builds the trie anew between chunks.
 */
#include "phrases.hpp"
#include <atomic>
#include <cstdio>
#include "esp_heap_caps.h"
#include "service_internal.hpp"
#include "transcript.hpp"

namespace phrases {

namespace {

struct List {
    const char *text = nullptr, *source = "built in";
    pieasr_phrase_t *items = nullptr;  /* PSRAM */
    transcript::Rewrite *rw = nullptr; /* PSRAM */
    int n = 0, bad = 0;
    int words[COLUMN] = {}, nw = 0, sent[COLUMN] = {}, ns = 0;
    std::atomic<bool> on{false};
    std::atomic<uint32_t> off{0};
};

List s_list[LISTS];
std::atomic<int> s_sel{0};
const pieasr_model_t *s_m;
pieasr_trie_t *s_trie;
std::atomic<bool> s_dirty{true};

void *psram(size_t n) { return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM); }
void release(void *p) { heap_caps_free(p); }

/* the list in use (list 0 until the one selected is configured) */
List &cur()
{
    const int s = s_sel.load();
    return s_list[s < lists() ? s : 0];
}

bool used(const List &l, int i)
{
    for (int j = 0; j < l.nw; j++)
        if (l.words[j] == i)
            return true;
    for (int j = 0; j < l.ns; j++)
        if (l.sent[j] == i)
            return true;
    return false;
}

} // namespace

int lists()
{
    int n = 1;
    for (int i = 0; i < asr::Config::MORE_LISTS && i + 1 < LISTS; i++)
        if (asr::g_config.more_phrase_files[i] || asr::g_config.more_default_phrases[i])
            n = i + 2;
    return n;
}

int selected() { return s_sel.load(); }

void select(int list)
{
    if (list < 0 || list >= LISTS)
        return;
    if (s_sel.exchange(list) != list)
        s_dirty = true;
}

int words() { return cur().nw; }
int sentences() { return cur().ns; }
int word(int j) { return cur().words[j]; }
int sentence(int j) { return cur().sent[j]; }
const pieasr_phrase_t &item(int i) { return cur().items[i]; }
int ignored() { return cur().bad; }
const char *source() { return cur().source; }
bool on() { return cur().on.load(); }
bool enabled(int i) { return !(cur().off.load() & (1u << i)); }
uint32_t off_mask() { return cur().off.load(); }

void set_on(bool v)
{
    cur().on = v;
    s_dirty = true;
}

void set_enabled(int i, bool v)
{
    List &l = cur();
    const uint32_t off = l.off.load();
    l.off = v ? off & ~(1u << i) : off | (1u << i);
    s_dirty = true;
}

void set_state(bool on, uint32_t off_mask)
{
    List &l = cur();
    l.on = on;
    l.off = off_mask;
    s_dirty = true;
}

} // namespace phrases

namespace asr {

using namespace phrases;

void phrases_set_text(const char *text, const char *source)
{
    s_list[0].text = text;
    s_list[0].source = source;
}

void phrases_set_list_text(int list, const char *text, const char *source)
{
    if (list <= 0 || list >= LISTS)
        return;
    s_list[list].text = text;
    s_list[list].source = source;
}

void phrases_init(const pieasr_model_t *m)
{
    s_m = m;
    s_trie = pieasr_trie_new(psram, release);
    const int nl = lists();
    for (int k = 0; k < nl; k++) {
        List &l = s_list[k];
        if (k > 0) {
            if (!l.text) /* not read from the card: the list's defaults */
                l.text = g_config.more_default_phrases[k - 1] ? g_config.more_default_phrases[k - 1] : "";
            l.on = true; /* a demo's list listens from the start */
        }
        l.items = static_cast<pieasr_phrase_t *>(psram(sizeof(pieasr_phrase_t) * PIEASR_PHRASES_MAX));
        l.rw = static_cast<transcript::Rewrite *>(psram(sizeof(transcript::Rewrite) * PIEASR_PHRASES_MAX *
                                                        PIEASR_PHRASE_FORMS));
        if (!l.items || !l.rw || !s_trie) {
            log("phrases: no memory");
            s_trie = nullptr; /* (phrases_apply does nothing then) */
            return;
        }
        l.n = pieasr_phrases_parse(l.text ? l.text : "", l.items, PIEASR_PHRASES_MAX, &l.bad);
        l.nw = l.ns = 0;
        for (int i = 0; i < l.n; i++) {
            if (l.items[i].word ? l.nw < COLUMN : l.ns < COLUMN)
                (l.items[i].word ? l.words[l.nw++] : l.sent[l.ns++]) = i;
            else
                l.bad++;
        }
        if (k == 0)
            log("phrases from %s: %d words, %d sentences, %d lines ignored", l.source, l.nw, l.ns, l.bad);
        else
            log("phrase list %d from %s: %d words, %d sentences, %d lines ignored", k, l.source, l.nw, l.ns, l.bad);
    }
    s_dirty = true;
}

void phrases_apply(pieasr_beam_t *b)
{
    if (!s_trie || !s_dirty.exchange(false))
        return;
    const List &l = cur();
    pieasr_trie_clear(s_trie);
    int nrw = 0, active = 0, seqs = 0;
    if (l.on.load()) {
        for (int i = 0; i < l.n; i++) {
            if (!used(l, i) || (l.off.load() & (1u << i)))
                continue;
            active++;
            const pieasr_phrase_t &it = l.items[i];
            for (int f = 0; f < it.nforms; f++) {
                const int k = pieasr_trie_add_text(s_trie, s_m, it.forms[f], BONUS);
                if (k < 0)
                    log("phrases: \"%s\" has a word the model's pieces cannot spell", it.forms[f]);
                else
                    seqs += k;
                if (f > 0)
                    l.rw[nrw++] = {it.forms[f], it.shown};
            }
        }
    }
    pieasr_trie_build(s_trie);
    transcript::set_rewrites(l.rw, nrw);
    pieasr_beam_set_phrases(b, active ? s_trie : nullptr);
    if (lists() > 1)
        log("phrase list %d %s: %d lines, %d sequences, %d trie nodes, %d rewrites", selected() < lists() ? selected() : 0,
            l.on.load() ? "on" : "off", active, seqs, pieasr_trie_nodes(s_trie), nrw);
    else
        log("phrases %s: %d lines, %d sequences, %d trie nodes, %d rewrites", l.on.load() ? "on" : "off", active, seqs,
            pieasr_trie_nodes(s_trie), nrw);
}

} // namespace asr
