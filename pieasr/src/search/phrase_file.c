// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The phrase file: one item per line, "shown" or "shown = spoken | spoken", '#' comments.
#include <string.h>
#include "pieasr/phrases.h"

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = 0;
    }
    return s;
}

static void collapse(char *s)   // runs of blanks -> one space
{
    char *w = s;
    for (char *p = s; *p; p++) {
        const int blank = *p == ' ' || *p == '\t';
        if (blank && (w == s || w[-1] == ' ')) {
            continue;
        }
        *w++ = blank ? ' ' : *p;
    }
    while (w > s && w[-1] == ' ') {
        w--;
    }
    *w = 0;
}

static int put(char *dst, const char *src)
{
    if (!*src || strlen(src) >= PIEASR_PHRASE_TEXT) {
        return 0;
    }
    strcpy(dst, src);
    return 1;
}

int pieasr_phrases_parse(const char *text, pieasr_phrase_t *items, int max, int *bad)
{
    int n = 0;
    *bad = 0;
    char line[512];
    for (const char *p = text; *p;) {
        const size_t k = strcspn(p, "\n"), c = k < sizeof line - 1 ? k : sizeof line - 1;
        memcpy(line, p, c);
        line[c] = 0;
        p += k + (p[k] == '\n');
        char *s = trim(line);
        if (!*s || *s == '#') {
            continue;
        }
        char *eq = strchr(s, '='), *rest = NULL;
        if (eq) {
            *eq = 0;
            rest = eq + 1;
        }
        char *shown = trim(s);
        collapse(shown);
        pieasr_phrase_t it;
        memset(&it, 0, sizeof it);
        if (n == max || !put(it.shown, shown)) {
            (*bad)++;
            continue;
        }
        strcpy(it.forms[0], it.shown);
        it.nforms = 1;
        if (rest) {
            char *save = NULL;
            for (char *f = strtok_r(rest, "|", &save); f; f = strtok_r(NULL, "|", &save)) {
                f = trim(f);
                collapse(f);
                if (it.nforms < PIEASR_PHRASE_FORMS && put(it.forms[it.nforms], f)) {
                    it.nforms++;
                } else if (*f) {
                    (*bad)++;
                }
            }
        }
        it.word = !strchr(it.shown, ' ');
        items[n++] = it;
    }
    return n;
}
