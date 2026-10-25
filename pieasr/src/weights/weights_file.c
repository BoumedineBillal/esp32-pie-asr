// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The weights file: its header, the tensor lookups, float16 conversion.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "weights/weights_file.h"

int pq_weights_open(pq_weights_t *b, const void *data, size_t size)
{
    const uint8_t *p = data;
    uint32_t h[6];
    if (size < 64) {
        return -1;
    }
    memcpy(h, p, sizeof(h));
    if (memcmp(p, "KRK1", 4) || h[1] < 1 || h[1] > 2 || h[5] > size) {
        return -1;
    }
    b->version = (int)h[1];
    b->base = p;
    b->n = (int)h[2];
    b->table = (const pq_weights_entry_t *)(p + h[3]);
    return 0;
}

const pq_weights_entry_t *pq_weights_find(const pq_weights_t *b, const char *name)
{
    int lo = 0, hi = b->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strncmp(name, b->table[mid].name, sizeof(b->table[mid].name));
        if (c == 0) {
            return &b->table[mid];
        }
        if (c < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}

static const pq_weights_entry_t *need(const pq_weights_t *b, const char *name, int kind)
{
    const pq_weights_entry_t *e = pq_weights_find(b, name);
    if (!e || e->kind != kind) {
        fprintf(stderr, "weights: %s %s (kind %d)\n", e ? "wrong kind for" : "missing", name, kind);
        abort();
    }
    return e;
}

pq_cqmat_t pq_weights_cq(const pq_weights_t *b, const char *name)
{
    const pq_weights_entry_t *e = need(b, name, PQ_KIND_CQ);
    pq_cqmat_t m = {
        .codes = b->base + e->off_a,
        .norms = (const uint16_t *)(b->base + e->off_b),
        .bits = e->bits_or_dtype,
        .G = (int)e->group,
        .out = (int)e->shape[0],
        .in = (int)e->shape[1],
    };
    return m;
}

const uint16_t *pq_weights_f16(const pq_weights_t *b, const char *name, int *count)
{
    const pq_weights_entry_t *e = need(b, name, PQ_KIND_F16);
    if (count) {
        int n = 1;
        for (int i = 0; i < e->ndim; i++) {
            n *= (int)e->shape[i];
        }
        *count = n;
    }
    return (const uint16_t *)(b->base + e->off_a);
}

const pq_weights_entry_t *pq_weights_q8(const pq_weights_t *b, const char *name)
{
    return need(b, name, PQ_KIND_Q8);
}

// branch-free for every finite value (subnormals included): the half's exponent and mantissa shifted into a float,
// then scaled by 2^112 (exact); the model's weights have no inf / nan
float pq_f16_to_f32(uint16_t h)
{
    uint32_t u = (uint32_t)(h & 0x7fff) << 13;
    float f;
    memcpy(&f, &u, 4);
    f *= 5.192296858534828e33f;   // 2^112
    return (h & 0x8000) ? -f : f;
}

void pq_f16_to_f32_vec(const uint16_t *src, float *dst, int n)
{
    for (int i = 0; i < n; i++) {
        dst[i] = pq_f16_to_f32(src[i]);
    }
}
