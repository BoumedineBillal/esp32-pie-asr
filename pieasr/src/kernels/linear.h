// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The matrix products with the 2/4-bit weights: in float (the reference), in int8 (plain C and in the PIE
// organization) and in integer arithmetic.
//   W[o, g*G:(g+1)*G] = (LV[code] / sqrt(G) * norm[o,g]) @ H_G     (H_G orthonormal Hadamard, symmetric)
// so  y[o] = sum_g norm[o,g] / G * sum_k LV[code[o, g*G+k]] * (Hu x_g)[k]
// with Hu the unnormalized Hadamard (entries +-1, Sylvester order): the input is rotated once per group with
// additions only, and that rotation is shared by every row.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "weights/weights_file.h"

// ---- float (linear_ref.c)
// y[t*ldy + o] = bias[o] (float16, may be NULL) + W x[t*ldx + :], t < T. scratch: T * in floats.
void pq_linear_ref(const pq_cqmat_t *w, const uint16_t *bias, const float *x, int T, int ldx, float *y, int ldy,
                   float *scratch);
// in-place unnormalized fast Walsh-Hadamard transform of n (power of 2) floats, Sylvester order
void pq_fwht_f32(float *v, int n);

// ---- int8, plain C (linear_ref.c): the arithmetic the PIE kernels implement, in its exact C form
//  - each rotated input group r = Hu x_g is quantized to int8 with its own scale sx[t,g] = max|r| / 127
//    (round to nearest even);
//  - the levels are int8: LQ = round(level * 127 / max|level|), scale lsc = max|level| / 127;
//  - acc[t,o,g] = sum_k LQ[code] * xq[k] (int32, exact);
//  - y[t,o] = bias[o] + sum_g (norm[o,g] / G * lsc * sx[t,g]) * acc   (float, g in order).
// scratch: T * in bytes (xq) + T * (in / G) floats (sx), float aligned.
void pq_linear_q8(const pq_cqmat_t *w, const uint16_t *bias, const float *x, int T, int ldx, float *y, int ldy,
                  void *scratch);
size_t pq_linear_q8_scratch(const pq_cqmat_t *w, int T);
extern const int8_t pq_lq2[4], pq_lq4[16];
extern float pq_lsc2, pq_lsc4;

// ---- int8 in the PIE organization (linear_pie.c): the same arithmetic and float order as pq_linear_q8, so the same
// results:
//  - the inputs are rotated and quantized once (int8, one scale per frame and group);
//  - per tile of 16 rows x G inputs: the codes are decoded to int8 column vectors (pq_decode2 / pq_decode4), then
//    pq_tile16 computes the 16 int32 sums of every frame; the float scales are applied in C.
void pq_pie_init(void);               // the decode tables
const uint32_t *pq_lut(int bits);     // the decode table of 2- or 4-bit codes
size_t pq_linear_pie_scratch(const pq_cqmat_t *w, int T);
void pq_linear_pie(const pq_cqmat_t *w, const uint16_t *bias, const float *x, int T, int ldx, float *y, int ldy,
                   void *scratch);
// the internal-RAM working buffers of the products (shared by pq_linear_pie and pq_linear_int, never both at once)
typedef struct {
    int8_t *xq;            // int8 inputs [T][in] (16-byte aligned)
    size_t xq_cap;
    void *sx;              // per frame and group values
    size_t sx_cap;
    int32_t *acc[2];       // per core [64][16] int32 (16-byte aligned)
    int8_t *wdec[2];       // per core decoded tile [128 * 16 + 32] (16-byte aligned)
} pq_bufs_t;
void pq_get_bufs(pq_bufs_t *b);
// cycles spent in each part of pq_linear_pie (mcycle on the chip; zeros elsewhere), summed over calls
typedef struct {
    uint64_t quant, decode, tile, post;
    uint32_t calls;
    uint64_t macs;
} pq_pie_prof_t;
extern pq_pie_prof_t pq_pie_prof;

// ---- integer arithmetic (linear_int.c). Per frame t, group g of G inputs, row o:
//   x16 = round(x * 32767 / ax)                  integer input (ax: calibrated max |input|); not clamped to int16
//                                                while the input is float (the Hadamard is int32)
//   r   = Hu x16                                 +-1 Hadamard of the group, int32, exact
//   e   = max(0, bitlen(max_k |r|) - 7)          power-of-2 scale of this frame and group
//   q   = sat8(round_even(r / 2^e))              int8 (ties to even; q = r when e = 0); pq_q8_shift
//   acc = 32 + sum_k LQ[code] q                  int32 (QACC, started at 32)
//   a16 = acc >> 6                               floor: the rounded sum / 64, fits int16
//   p   = (a16 n16[o,g]) >> max(0, S - e)        floor; n16 = round(norm * 32767 / max norm) (per matrix)
//   y32 = 128 + round(bias / U) + sum_g p        int32 (adds saturate)
//   y16 = y32 >> 8                               output, value y16 * sy (sat16 once the output is int16)
// with U = 64 * sn * lsc / G * sx * 2^S, sn = max norm / 32767, sx = ax / 32767, lsc = pq_lsc2 / pq_lsc4,
// sy = 256 U, and S the smallest integer with 32767 sy >= ay (ay: calibrated max |output|).
// The PIE kernel pq_tile16_n does acc, a16, p and the sum over g; the rest is C.
typedef struct {
    int16_t *n16;          // group norms, int16, in tiles: [out / 16][in / G][16]; NULL: not prepared
    float inv_sx;          // 32767 / ax
    float inv_u, sy;       // 1 / U, U 2^n (the value of one output step)
    int S;
    int n;                 // the output is y32 >> n (8; pqi_prepare_res: chosen so that U 2^n = the residual scale)
} pqi_t;

// prepare q for matrix w from the calibrated ax, ay; n16 = storage for out * in / G int16 (16-byte aligned; may be
// w->norms itself: the float16 norms are read first). tmp: out * in / G uint16 of scratch.
void pqi_prepare(pqi_t *q, const pq_cqmat_t *w, float ax, float ay, int16_t *n16, uint16_t *tmp);
// the same for a matrix whose output is added into an int16 residual of scale s_res (PQI_POST_ADD16): the group norms
// are scaled by 1 / c, c in [1, 2) (n16 at most 32767 / c), so that U 2^n = s_res exactly for an integer n; then
//   x16 = sat16(x16 + (y32 >> n))          (y32 starts at 2^(n-1): rounded). Returns n, or -1 (q as pqi_prepare)
int pqi_prepare_res(pqi_t *q, const pq_cqmat_t *w, float ax, float ay, float s_res, int16_t *n16, uint16_t *tmp);

// y[t * ldy + o] = W x[t * ldx + :] + bias (float in, float out: y = y16 * sy), t < T <= 64, then post:
//   PQI_POST_NONE: y = v;  PQI_POST_SWOOSH_L: y = SwooshL(v) (table);  PQI_POST_ADD: y += v (a residual add)
// (fused into the output loop: one pass over y instead of two; y and the workspace live in PSRAM)
// scratch (used when the int8 inputs do not fit the internal buffer): pq_linear_int_scratch bytes
enum { PQI_POST_NONE = 0, PQI_POST_SWOOSH_L = 1, PQI_POST_ADD = 2, PQI_POST_ADD16 = 3 };
void pq_linear_int(const pq_cqmat_t *w, const pqi_t *q, const uint16_t *bias, const float *x, int T, int ldx, float *y,
                   int ldy, void *scratch, int post);
size_t pq_linear_int_scratch(const pq_cqmat_t *w, int T);

// int16 activations: the same product with an int16 input x16 (value x16 * ax / 32767: exactly the product's
// integer input, no conversion) and / or an int16 output through an activation table (value h * sh)
typedef struct {
    const float *x;        // float input, or NULL: x16
    const int16_t *x16;    // int16 input (16-byte aligned, ldx a multiple of 8)
    int T, ldx;
    float *y;              // float output (post as above), or NULL: y16
    int16_t *y16;          // int16 output h = tab(y16), or with post PQI_POST_ADD16 the int16 residual it is added
                           // into (pqi_prepare_res) (16-byte aligned, ldy a multiple of 8)
    const uint32_t *tab;   // pq_tab_build for the rows in [tab_row0, tab_row1) (tab_row1 0: no end); the other
    int tab_row0, tab_row1;   // rows: the identity, h = v
    int ldy, post;
    int res_j;             // PQI_POST_ADD16: the residual's scale grows by 2^res_j (pq_out_add16)
    int8_t *y8;            // or int8 output per channel: y8 = sat(round((y32 >> 8) N8[o] / 2^sh8), -127, 127)
    const int16_t *N8;     // (pq_out_act8 with M = 1, B = 0, no table); y8 16-byte aligned, ldy a multiple of 16
    int sh8;
    void *scratch;
} pqi_io_t;
void pq_linear_int_io(const pq_cqmat_t *w, const pqi_t *q, const uint16_t *bias, const pqi_io_t *io);

// the activation tables (kernels.h, pq_tab_eval): f sampled every 64 output steps, or every 2^k (pq_tab_build_k)
void pq_tab_build(uint32_t *tab, float (*f)(float), float sy, float sh);
void pq_tab_build_k(uint32_t *tab, float (*f)(float), float sy, float sh, int k);

typedef struct {
    uint64_t quant, tiles;     // cycles of the two phases (core 0, including its wait for core 1)
    uint64_t q_fwht, q_max, q_round;          // core 0's own work in phase 1: conversion + Hadamard, max, int8
    uint64_t t_decode, t_tile, t_rest;        // core 0's own work in phase 2: decode, pq_tile16_n, bias / output
    uint64_t left, groups;     // groups with S < e (a left shift would be needed; the spec clamps) / all
} pqi_prof_t;
extern pqi_prof_t pqi_prof;
