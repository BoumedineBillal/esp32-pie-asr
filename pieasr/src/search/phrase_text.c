// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// A phrase's text as the model's pieces: each word split every way the vocabulary allows (no SentencePiece model
// exists for these pieces), the fewest-piece splits kept, the first word also capitalized; each combination of the
// words' splits added to the trie.
#include <string.h>
#include "search/search_internal.h"

#define V PIEASR_VOCAB

static const char WB[] = "\xe2\x96\x81";   // U+2581, a word's start

#define PH 2048   // a hash of the vocabulary's pieces (650 of them), from the trie's alloc (PSRAM on the chip)
static int16_t *s_ph;
static const pieasr_model_t *s_ph_m;

static uint32_t fnv(const char *s, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ (uint8_t)s[i]) * 16777619u;
    }
    return h;
}

static int pieces_init(const pieasr_model_t *m, void *(*alloc)(size_t))
{
    if (s_ph_m == m) {
        return 0;
    }
    if (!s_ph && !(s_ph = alloc(sizeof(int16_t) * PH))) {
        return -1;
    }
    for (int i = 0; i < PH; i++) {
        s_ph[i] = -1;
    }
    for (int i = 3; i < V; i++) {   // (0..2: <blk>, <sos/eos>, <unk>)
        if (!m->tokens[i] || !m->tokens[i][0]) {
            continue;
        }
        uint32_t h = fnv(m->tokens[i], strlen(m->tokens[i])) & (PH - 1);
        while (s_ph[h] >= 0) {
            h = (h + 1) & (PH - 1);
        }
        s_ph[h] = (int16_t)i;
    }
    s_ph_m = m;
    return 0;
}

static int piece_id(const pieasr_model_t *m, const char *s, size_t n)
{
    for (uint32_t h = fnv(s, n) & (PH - 1); s_ph[h] >= 0; h = (h + 1) & (PH - 1)) {
        const char *t = m->tokens[s_ph[h]];
        if (strlen(t) == n && !memcmp(t, s, n)) {
            return s_ph[h];
        }
    }
    return -1;
}

// a piece for word[a, b), word-initial (U+2581 in front) or not
static int piece_of(const pieasr_model_t *m, const char *w, int a, int b, int initial)
{
    char buf[PIEASR_PHRASE_TEXT + 4];
    const int n = b - a;
    if (initial) {
        memcpy(buf, WB, 3);
        memcpy(buf + 3, w + a, (size_t)n);
        return piece_id(m, buf, (size_t)n + 3);
    }
    return piece_id(m, w + a, (size_t)n);
}

#define SPLITS 3      // splits kept per word
#define MAXP 48       // pieces per word
typedef struct {
    int16_t p[SPLITS][MAXP];
    int n[SPLITS];
    int count;
} splits_t;

typedef struct {
    const pieasr_model_t *m;
    const char *w;
    int len, bound, mc[PIEASR_PHRASE_TEXT + 1];
    int16_t cur[MAXP];
    splits_t *out;
} split_ctx_t;

static void split_rec(split_ctx_t *c, int pos, int depth)
{
    if (c->out->count == SPLITS || depth + c->mc[pos] > c->bound) {
        return;
    }
    if (pos == c->len) {
        memcpy(c->out->p[c->out->count], c->cur, sizeof(int16_t) * (size_t)depth);
        c->out->n[c->out->count++] = depth;
        return;
    }
    for (int j = c->len; j > pos && depth < MAXP; j--) {
        const int id = piece_of(c->m, c->w, pos, j, 0);
        if (id >= 0) {
            c->cur[depth] = (int16_t)id;
            split_rec(c, j, depth + 1);
        }
    }
}

// the splits of one word, fewest pieces first (up to SPLITS, at most one piece more than the fewest): a word that is
// one word-initial piece is only that; otherwise a word-initial first piece or the bare U+2581 then others
static void split_word(const pieasr_model_t *m, const char *w, int len, splits_t *out)
{
    out->count = 0;
    const int whole = piece_of(m, w, 0, len, 1);
    if (whole >= 0) {
        out->p[0][0] = (int16_t)whole;
        out->n[0] = 1;
        out->count = 1;
        return;
    }
    split_ctx_t c = {.m = m, .w = w, .len = len, .out = out};
    // the fewest non-initial pieces from each position to the end
    c.mc[len] = 0;
    for (int a = len - 1; a >= 0; a--) {
        c.mc[a] = 1 << 20;
        for (int b = a + 1; b <= len; b++) {
            if (c.mc[b] < (1 << 20) && piece_of(m, w, a, b, 0) >= 0 && 1 + c.mc[b] < c.mc[a]) {
                c.mc[a] = 1 + c.mc[b];
            }
        }
    }
    int best = 1 << 20;
    for (int j = 1; j <= len; j++) {
        if (piece_of(m, w, 0, j, 1) >= 0 && c.mc[j] < (1 << 20) && 1 + c.mc[j] < best) {
            best = 1 + c.mc[j];
        }
    }
    const int bare = piece_id(m, WB, 3);
    if (bare >= 0 && c.mc[0] < (1 << 20) && 1 + c.mc[0] < best) {
        best = 1 + c.mc[0];
    }
    if (best >= (1 << 20)) {
        return;
    }
    // every split within one piece of the fewest, collected by depth-first search, then sorted by length
    splits_t all = {.count = 0};
    for (c.bound = best; c.bound <= best + 1 && all.count < SPLITS; c.bound++) {
        for (int j = len; j >= 1; j--) {
            const int id = piece_of(m, w, 0, j, 1);
            if (id < 0) {
                continue;
            }
            splits_t one = {.count = 0};
            c.out = &one;
            c.cur[0] = (int16_t)id;
            split_rec(&c, j, 1);
            for (int s = 0; s < one.count && all.count < SPLITS; s++) {
                int dup = 0;
                for (int t = 0; t < all.count; t++) {
                    dup |= all.n[t] == one.n[s] && !memcmp(all.p[t], one.p[s], sizeof(int16_t) * (size_t)one.n[s]);
                }
                if (!dup) {
                    memcpy(all.p[all.count], one.p[s], sizeof(int16_t) * (size_t)one.n[s]);
                    all.n[all.count++] = one.n[s];
                }
            }
        }
        if (bare >= 0) {
            splits_t one = {.count = 0};
            c.out = &one;
            c.cur[0] = (int16_t)bare;
            split_rec(&c, 0, 1);
            for (int s = 0; s < one.count && all.count < SPLITS; s++) {
                int dup = 0;
                for (int t = 0; t < all.count; t++) {
                    dup |= all.n[t] == one.n[s] && !memcmp(all.p[t], one.p[s], sizeof(int16_t) * (size_t)one.n[s]);
                }
                if (!dup) {
                    memcpy(all.p[all.count], one.p[s], sizeof(int16_t) * (size_t)one.n[s]);
                    all.n[all.count++] = one.n[s];
                }
            }
        }
    }
    // fewest pieces first (stable)
    for (int i = 1; i < all.count; i++) {
        for (int j = i; j > 0 && all.n[j] < all.n[j - 1]; j--) {
            int16_t tp[MAXP];
            memcpy(tp, all.p[j], sizeof tp);
            memcpy(all.p[j], all.p[j - 1], sizeof tp);
            memcpy(all.p[j - 1], tp, sizeof tp);
            const int tn = all.n[j];
            all.n[j] = all.n[j - 1];
            all.n[j - 1] = tn;
        }
    }
    *out = all;
}

#define MAXW 16       // words per phrase
#define COMBOS 12     // sequences kept per spelling of the first word

typedef struct {   // pieasr_trie_add_text's work (from the trie's alloc: PSRAM on the chip, freed after)
    splits_t sp[MAXW];
    int idx[COMBOS * 8][MAXW], tot[COMBOS * 8], order[COMBOS * 8];
    int seq[MAXW * MAXP];
} text_work_t;

static int add_text(pieasr_trie_t *g, const pieasr_model_t *m, const char *text, float bonus, text_work_t *wk);

int pieasr_trie_add_text(pieasr_trie_t *g, const pieasr_model_t *m, const char *text, float bonus)
{
    if (pieces_init(m, g->alloc)) {
        return -1;
    }
    text_work_t *wk = g->alloc(sizeof(text_work_t));
    if (!wk) {
        return -1;
    }
    const int r = add_text(g, m, text, bonus, wk);
    g->release(wk);
    return r;
}

static int add_text(pieasr_trie_t *g, const pieasr_model_t *m, const char *text, float bonus, text_work_t *wk)
{
    char buf[PIEASR_PHRASE_TEXT];
    if (strlen(text) >= sizeof buf) {
        return -1;
    }
    strcpy(buf, text);
    char *words[MAXW];
    int nw = 0;
    char *save = NULL;
    for (char *t = strtok_r(buf, " ", &save); t && nw < MAXW; t = strtok_r(NULL, " ", &save)) {
        words[nw++] = t;
    }
    if (!nw) {
        return -1;
    }
    splits_t *sp = wk->sp;
    int added = 0;
    for (int cap = 0; cap < 2; cap++) {   // the first word as written, then capitalized (a sentence's start)
        if (cap && !(words[0][0] >= 'a' && words[0][0] <= 'z')) {
            break;
        }
        for (int i = 0; i < nw; i++) {
            char wb[PIEASR_PHRASE_TEXT];
            strcpy(wb, words[i]);
            if (i == 0 && cap) {
                wb[0] = (char)(wb[0] - 'a' + 'A');
            }
            split_word(m, wb, (int)strlen(wb), &sp[i]);
            if (!sp[i].count) {
                return cap ? added : -1;
            }
        }
        // the combinations (one split per word), fewest pieces first: index vectors in order, then a stable sort
        int(*idx)[MAXW] = wk->idx, *tot = wk->tot, *order = wk->order;
        int nc = 0, v[MAXW] = {0};
        for (;;) {
            if (nc < COMBOS * 8) {
                int t = 0;
                for (int i = 0; i < nw; i++) {
                    t += sp[i].n[v[i]];
                    idx[nc][i] = v[i];
                }
                tot[nc++] = t;
            }
            int i = nw - 1;
            while (i >= 0 && ++v[i] == sp[i].count) {
                v[i--] = 0;
            }
            if (i < 0) {
                break;
            }
        }
        for (int i = 0; i < nc; i++) {
            order[i] = i;
        }
        for (int i = 1; i < nc; i++) {
            for (int j = i; j > 0 && tot[order[j]] < tot[order[j - 1]]; j--) {
                const int t = order[j];
                order[j] = order[j - 1];
                order[j - 1] = t;
            }
        }
        for (int c = 0; c < nc && c < COMBOS; c++) {
            int *seq = wk->seq, n = 0;
            for (int i = 0; i < nw; i++) {
                const int s = idx[order[c]][i];
                for (int k = 0; k < sp[i].n[s]; k++) {
                    seq[n++] = sp[i].p[s][k];
                }
            }
            if (pieasr_trie_add_tokens(g, seq, n, bonus)) {
                return -1;
            }
            added++;
        }
    }
    return added;
}
