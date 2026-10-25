// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// Phrase biasing on hand-made data: the trie's bonuses, the phrase file's parser, the splitter of texts into pieces
// (on a small vocabulary).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "model_internal.h"
#include "search/search_internal.h"

static int fails;

static void check(const char *what, double got, double want)
{
    const int ok = fabs(got - want) < 1e-5;
    fails += !ok;
    printf("%s %-70s %+.2f (want %+.2f)\n", ok ? "PASS" : "FAIL", what, got, want);
}

static void *zalloc(size_t n)
{
    return calloc(1, n);
}

// the bonus collected along tok (state left in *state)
static float walk(const pieasr_trie_t *g, const int *tok, int n, int *state)
{
    float total = 0.0f;
    int s = 0;
    for (int i = 0; i < n; i++) {
        float d;
        s = pq_trie_step(g, s, tok[i], &d);
        total += d;
    }
    *state = s;
    return total;
}

static void test_trie(void)
{
    pieasr_trie_t *g = pieasr_trie_new(zalloc, free);
    pieasr_trie_build(g);
    int s;
    const int any[] = { 5, 6, 7, 8 };
    check("empty list: no bonus on any path", walk(g, any, 4, &s), 0.0);
    pieasr_trie_clear(g);
    const int p1[] = { 10, 11, 12 }, p2[] = { 11, 13 };
    pieasr_trie_add_tokens(g, p1, 3, 1.5f);
    pieasr_trie_add_tokens(g, p2, 2, 1.5f);
    pieasr_trie_build(g);
    const int full[] = { 10, 11, 12 }, broken[] = { 10, 11, 99 }, part[] = { 7, 10, 11 }, overlap[] = { 10, 11, 13 },
              twice[] = { 11, 13, 5, 11, 13 };
    check("10 11 12 complete: 3 x 1.5 kept", walk(g, full, 3, &s) + pq_trie_final(g, s), 4.5);
    check("10 11 then 99: the partial bonus removed", walk(g, broken, 3, &s), 0.0);
    check("7 10 11 at the end: removed by final", walk(g, part, 3, &s) + pq_trie_final(g, s), 0.0);
    check("10 11 13: falls over to the phrase 11 13", walk(g, overlap, 3, &s), 3.0);
    check("11 13 5 11 13: the phrase twice", walk(g, twice, 5, &s), 6.0);
    check("the nodes: root + 3 + 2", pieasr_trie_nodes(g), 6);
    pieasr_trie_clear(g);
    const int longer[] = { 20, 21, 22, 23 }, shorter[] = { 20, 21 };
    pieasr_trie_add_tokens(g, longer, 4, 1.0f);
    pieasr_trie_add_tokens(g, shorter, 2, 1.0f);
    pieasr_trie_build(g);
    const int both[] = { 20, 21, 22, 23 }, only_short[] = { 20, 21, 99 }, short_part[] = { 20, 21, 22 };
    check("a phrase that starts a longer one: the longer completes through it",
          walk(g, both, 4, &s) + pq_trie_final(g, s), 4.0);
    check("  ... the shorter alone keeps its bonus", walk(g, only_short, 3, &s) + pq_trie_final(g, s), 2.0);
    check("  ... the shorter, then an unfinished longer", walk(g, short_part, 3, &s) + pq_trie_final(g, s), 2.0);
    pieasr_trie_free(g);
}

static void test_file(void)
{
    pieasr_phrase_t it[4];
    int bad;
    const char *file = "# a note\n  ESP32 =  ESP thirty two | ESP thirty-two \n\nturn on   the light\r\n";
    const int n = pieasr_phrases_parse(file, it, 4, &bad);
    check("the file: items", n, 2);
    check("  ... lines not understood", bad, 0);
    check("  ... ESP32's forms (itself and two spoken)", it[0].nforms, 3);
    check("  ... the second spoken form", strcmp(it[0].forms[2], "ESP thirty-two") == 0, 1);
    check("  ... a word", it[0].word, 1);
    check("  ... spaces collapsed, a sentence", strcmp(it[1].shown, "turn on the light") == 0 && !it[1].word, 1);
    const int n2 = pieasr_phrases_parse("one\ntwo\nthree\n= nothing shown\n", it, 2, &bad);
    check("more items than room: the first two kept", n2, 2);
    check("  ... the rest and the line without a shown text not understood", bad, 2);
}

// a vocabulary of a few pieces: hello as one word-initial piece; world only as wor + ld or w + or + ld
static void test_splitter(void)
{
    pieasr_model_t *m = zalloc(pieasr_model_size());
    m->tokens[3] = "\xe2\x96\x81hello";
    m->tokens[4] = "\xe2\x96\x81wor";
    m->tokens[5] = "ld";
    m->tokens[6] = "\xe2\x96\x81w";
    m->tokens[7] = "or";
    m->tokens[8] = "\xe2\x96\x81";
    m->tokens[9] = "w";
    pieasr_trie_t *g = pieasr_trie_new(zalloc, free);
    check("hello: its one piece (Hello has none)", pieasr_trie_add_text(g, m, "hello", 1.0f), 1);
    check("world: the fewest pieces and one more (2 splits)", pieasr_trie_add_text(g, m, "world", 1.0f), 2);
    check("hello world: one split of hello times two of world", pieasr_trie_add_text(g, m, "hello world", 1.0f), 2);
    check("xyz: no split", pieasr_trie_add_text(g, m, "xyz", 1.0f), -1);
    pieasr_trie_build(g);
    int s;
    const int hello[] = { 3 }, wor_ld[] = { 4, 5 }, w_or_ld[] = { 6, 7, 5 }, bare[] = { 8, 9, 7, 5 },
              hw[] = { 3, 4, 5 }, hw3[] = { 3, 6, 7, 5 };
    check("the trie: hello complete", walk(g, hello, 1, &s) + pq_trie_final(g, s), 1.0);
    check("  ... wor ld complete", walk(g, wor_ld, 2, &s) + pq_trie_final(g, s), 2.0);
    check("  ... w or ld complete", walk(g, w_or_ld, 3, &s) + pq_trie_final(g, s), 3.0);
    check("  ... the bare word start + w or ld (two more than the fewest): not added",
          walk(g, bare, 4, &s) + pq_trie_final(g, s), 0.0);
    check("  ... hello wor ld complete", walk(g, hw, 3, &s) + pq_trie_final(g, s), 3.0);
    check("  ... hello w or ld complete", walk(g, hw3, 4, &s) + pq_trie_final(g, s), 4.0);
    pieasr_trie_free(g);
    free(m);
}

int main(void)
{
    test_trie();
    test_file();
    test_splitter();
    printf("%s: %d failed\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
