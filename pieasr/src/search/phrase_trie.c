// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The phrase trie (Aho-Corasick over token sequences) and its bonuses: a hypothesis that follows a phrase gets the
// bonus per token, loses what it got if the phrase breaks off, keeps it once the phrase is complete.
#include <string.h>
#include "search/search_internal.h"

pieasr_trie_t *pieasr_trie_new(void *(*alloc)(size_t), void (*release)(void *))
{
    pieasr_trie_t *g = alloc(sizeof *g);
    if (!g) {
        return NULL;
    }
    g->alloc = alloc;
    g->release = release;
    g->cap = 256;
    g->nodes = alloc(sizeof(pq_trie_node_t) * (size_t)g->cap);
    if (!g->nodes) {
        release(g);
        return NULL;
    }
    pieasr_trie_clear(g);
    return g;
}

void pieasr_trie_clear(pieasr_trie_t *g)
{
    g->n = 1;
    g->nodes[0] = (pq_trie_node_t){.token = -1, .parent = -1, .fail = 0, .child = -1, .sibling = -1};
}

void pieasr_trie_free(pieasr_trie_t *g)
{
    if (!g) {
        return;
    }
    if (g->nodes) {
        g->release(g->nodes);
    }
    g->release(g);
}

int pieasr_trie_nodes(const pieasr_trie_t *g)
{
    return g->n;
}

static int child_of(const pieasr_trie_t *g, int s, int tok)
{
    for (int c = g->nodes[s].child; c >= 0; c = g->nodes[c].sibling) {
        if (g->nodes[c].token == tok) {
            return c;
        }
    }
    return -1;
}

int pieasr_trie_add_tokens(pieasr_trie_t *g, const int *tok, int n, float bonus)
{
    int s = 0;
    for (int i = 0; i < n; i++) {
        int c = child_of(g, s, tok[i]);
        if (c < 0) {
            if (g->n == g->cap) {
                pq_trie_node_t *bigger = g->alloc(sizeof(pq_trie_node_t) * (size_t)g->cap * 2);
                if (!bigger) {
                    return -1;
                }
                memcpy(bigger, g->nodes, sizeof(pq_trie_node_t) * (size_t)g->n);
                g->release(g->nodes);
                g->nodes = bigger;
                g->cap *= 2;
            }
            c = g->n++;
            g->nodes[c] = (pq_trie_node_t){.token = tok[i], .parent = s, .bonus = bonus, .fail = 0, .child = -1,
                                           .sibling = g->nodes[s].child};
            g->nodes[s].child = c;
        }
        s = c;
    }
    g->nodes[s].end = 1;
    return 0;
}

void pieasr_trie_build(pieasr_trie_t *g)
{
    // breadth-first: the scores (a phrase added later may end at a node that already has children), the fail arcs;
    // the queue in the nodes' own order is not breadth-first, so it is kept in a separate array
    int *queue = g->alloc(sizeof(int) * (size_t)g->n), head = 0, tail = 0;
    if (!queue) {
        return;
    }
    g->nodes[0].score = g->nodes[0].base = 0.0f;
    for (int c = g->nodes[0].child; c >= 0; c = g->nodes[c].sibling) {
        g->nodes[c].fail = 0;
        queue[tail++] = c;
    }
    while (head < tail) {
        const int u = queue[head++];
        pq_trie_node_t *nu = &g->nodes[u];
        nu->score = g->nodes[nu->parent].base + nu->bonus;
        nu->base = nu->end ? 0.0f : nu->score;
        for (int v = nu->child; v >= 0; v = g->nodes[v].sibling) {
            int f = nu->fail, c;
            while ((c = child_of(g, f, g->nodes[v].token)) < 0 && f != 0) {
                f = g->nodes[f].fail;
            }
            g->nodes[v].fail = c >= 0 && c != v ? c : 0;
            queue[tail++] = v;
        }
    }
    g->release(queue);
}

int pq_trie_step(const pieasr_trie_t *g, int state, int tok, float *delta)
{
    int s = state, c;
    while ((c = child_of(g, s, tok)) < 0 && s != 0) {
        s = g->nodes[s].fail;
    }
    const int next = c >= 0 ? c : 0;
    *delta = g->nodes[next].score - g->nodes[state].base;
    return next;
}

float pq_trie_final(const pieasr_trie_t *g, int state)
{
    return -g->nodes[state].base;
}
