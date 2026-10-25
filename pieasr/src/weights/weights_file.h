// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The weights file: a header, a table of named tensors sorted by name, the tensors (each 64-byte aligned); used in
// place.
#pragma once
#include <stddef.h>
#include <stdint.h>

enum { PQ_KIND_CQ = 1, PQ_KIND_F16 = 2, PQ_KIND_Q8 = 3, PQ_KIND_TOKENS = 4 };
// entry flags: bit 0 (PQ_KIND_Q8) the stored array is the transpose; bit 1 (PQ_KIND_Q8) the region after off_a has
// room for its dense tiled copy (pq_dense_bytes), which the in-place preparation writes there
#define PQ_Q8_SLACK 2

typedef struct __attribute__((packed)) {
    char name[88];
    uint8_t kind, bits_or_dtype, ndim, flags;
    uint32_t shape[4];
    uint32_t off_a, off_b;
    float scale;
    int32_t zero_point;
    uint32_t group;
} pq_weights_entry_t;

typedef struct {
    const uint8_t *base;
    const pq_weights_entry_t *table;
    int n;
    int version;   // 2 for the model (tiled codes)
} pq_weights_t;

// a 2/4-bit matrix [out, in]: level indices per group of G inputs (rotated domain), float16 norm per group.
// codes are in tiles of 16 rows x G inputs: tile (rb, g) at (rb * nG + g) * G * 2 * bits bytes;
// input k of the tile takes 2 * bits bytes: 2-bit: byte j = rows 4j..4j+3 (row 4j+r at bits 2r);
// 4-bit: byte j = rows 2j (low nibble), 2j+1 (high nibble)
typedef struct {
    const uint8_t *codes;
    const uint16_t *norms;
    int bits, G, out, in;
} pq_cqmat_t;

int pq_weights_open(pq_weights_t *b, const void *data, size_t size);   // 0 = ok

// the level index of weight (o, i) of a tiled cq matrix
static inline int pq_cq_code(const pq_cqmat_t *w, int o, int i)
{
    const int ng = w->in / w->G, rb = o >> 4, r = o & 15, g = i / w->G, k = i % w->G;
    const uint8_t *t = w->codes + (size_t)(rb * ng + g) * w->G * 2 * w->bits + k * 2 * w->bits;
    return w->bits == 2 ? (t[r >> 2] >> (2 * (r & 3))) & 3 : (t[r >> 1] >> (4 * (r & 1))) & 15;
}
const pq_weights_entry_t *pq_weights_find(const pq_weights_t *b, const char *name);
// lookups that abort with the name when missing or of the wrong kind
pq_cqmat_t pq_weights_cq(const pq_weights_t *b, const char *name);
const uint16_t *pq_weights_f16(const pq_weights_t *b, const char *name, int *count);
const pq_weights_entry_t *pq_weights_q8(const pq_weights_t *b, const char *name);

float pq_f16_to_f32(uint16_t h);
void pq_f16_to_f32_vec(const uint16_t *src, float *dst, int n);
