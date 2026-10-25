// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// Dense int8 matrix products on the tile kernel (pq_tile16): weights int8 with one scale per output row, stored in
// tiles of 16 rows ([out/16][K][16]); inputs quantized per frame to int8 (max |x| / 127, round to nearest even).
//   y[t, o] = (wscale[o] * xscale[t]) * sum_k wq[o, k] xq[t, k] + bias[o]
// K is padded to a multiple of 16 (zero weights).
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int8_t *w;        // [out/16][Kp][16]
    float *wscale;    // [out]
    float *bias;      // [out] or NULL
    int out, K, Kp;
} pq_dense_t;

// from float weights w[o*ldw + k] (row-major [out, K]); out is padded to a multiple of 16 internally
int pq_dense_from_f32(pq_dense_t *d, const float *w, int out, int K, int ldw, const float *bias,
                      void *(*alloc)(size_t));
// from an int8 / uint8 matrix as released: value = (q - zp) * scale; stored [K][out] when transposed, else [out][K]
// (zp 0 int8 matrices are kept exactly: wscale = scale)
int pq_dense_from_q8(pq_dense_t *d, const uint8_t *q, int is_uint8, int transposed, int out, int K, float scale,
                     int zp, const float *bias, void *(*alloc)(size_t));
// the bytes pq_dense_from_* asks its allocator for the tiles (out rounded to 16, Kp, 32 bytes of guard, 15 of
// alignment): the weights file leaves this much room after the joiner's two int8 sources (PQ_Q8_SLACK)
size_t pq_dense_bytes(int out, int K);
// y[t*ldy + o] for t < T, x[t*ldx + k]; any T (processed 64 frames at a time); post (PQ_DENSE_POST_*) is applied to
// every output value
enum { PQ_DENSE_POST_NONE = 0, PQ_DENSE_POST_SWOOSH_R = 1, PQ_DENSE_POST_SWOOSH_L = 2 };
void pq_dense_apply(const pq_dense_t *d, const float *x, int T, int ldx, float *y, int ldy, int post);
// the same with int8 ends (static scales, per tensor):
//  - xq != NULL: the input is already int8, value = xq * xscale, rows of d->Kp bytes (16-byte aligned, zero padded)
//  - yq != NULL: the output (after post) is written as int8 sat8(round(v * inv_ys[o])), one scale per output, rows
//    of ldy bytes
void pq_dense_apply_ex(const pq_dense_t *d, const float *x, const int8_t *xq, float xscale, int T, int ldx, float *y,
                       int8_t *yq, const float *inv_ys, int ldy, int post);

// cycles of core 0's own work in pq_dense_apply (zeros off the chip), summed over calls
typedef struct {
    uint64_t quant, tile, post;
    uint32_t calls;
} pq_dense_prof_t;
extern pq_dense_prof_t pq_dense_prof;
