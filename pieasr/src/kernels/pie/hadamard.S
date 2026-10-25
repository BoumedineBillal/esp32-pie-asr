// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The integer Hadamard kernels on PIE. C reference: ../c/hadamard.c.
#include "macros.inc"

    .text

// uint32_t pq_fwht_i32(int32_t *r, int G)        in-place Hadamard (Sylvester order, unnormalized) of G int32
// uint32_t pq_fwht_i16(const int16_t *x, int32_t *r, int G)     the same of G int16 inputs, written to r
// G = 16..128 (a power of 2), r (and x) 16-byte aligned. Returns max |r| (the C version returns the OR of |r|: the
// same bit length). Pass 1: the two in-vector stages (index bits 0, 1) on pairs of vectors via vunzip.32, back in
// natural order; then the vector stages h = 4 .. G/2 in radix-4 passes, the last one or two also taking vmax / vmin.
FUNC pq_fwht_i32
    mv      a2, a0
    srli    a3, a1, 3                   // pairs of vectors
1:
    esp.vld.128.ip  q0, a2, 16
    esp.vld.128.ip  q1, a2, -16
    esp.vunzip.32   q0, q1              // q0 = a0 a2 b0 b2, q1 = a1 a3 b1 b3
    esp.vadd.s32    q2, q0, q1
    esp.vsub.s32    q3, q0, q1
    esp.vunzip.32   q2, q3              // q2 = sa0 sb0 da0 db0, q3 = sa1 sb1 da1 db1
    esp.vadd.s32    q0, q2, q3          // ra0 rb0 ra1 rb1
    esp.vsub.s32    q1, q2, q3          // ra2 rb2 ra3 rb3
    esp.vunzip.32   q0, q1              // ra0..ra3, rb0..rb3
    esp.vst.128.ip  q0, a2, 16
    esp.vst.128.ip  q1, a2, 16
    addi    a3, a3, -1
    bnez    a3, 1b
    j       .Lfw_stages

FUNC pq_fwht_i16
    mv      a3, a2                      // G
    mv      a2, a1                      // r (walking)
    mv      a4, a0                      // x (walking)
    mv      a0, a1
    mv      a1, a3
    srli    a3, a1, 3
1:
    esp.vld.128.ip  q2, a4, 16          // 8 int16
    esp.vext.s16    q0, q1, q2          // q0 = x0..x3, q1 = x4..x7 as int32
    esp.vunzip.32   q0, q1
    esp.vadd.s32    q2, q0, q1
    esp.vsub.s32    q3, q0, q1
    esp.vunzip.32   q2, q3
    esp.vadd.s32    q0, q2, q3
    esp.vsub.s32    q1, q2, q3
    esp.vunzip.32   q0, q1
    esp.vst.128.ip  q0, a2, 16
    esp.vst.128.ip  q1, a2, 16
    addi    a3, a3, -1
    bnez    a3, 1b
.Lfw_stages:                            // a0 = r, a1 = G
    // vector stages (half blocks hb = 16 .. 2G bytes): radix-4 passes (stages hb, 2hb) while 3 or more remain,
    // then the last one or two stages in one pass that also takes vmax / vmin (q6, q7). Radix-4 butterfly of
    // a, b, c, d (at p, p + hb, p + 2hb, p + 3hb) in q0..q4: a+b+c+d, a-b+c-d, a+b-c-d, a-b-c+d
    slli    a4, a1, 1                   // the last stage's half block, bytes
    slli    t3, a1, 2
    add     t3, a0, t3                  // r + G
    li      a5, 16                      // half block, bytes
2:
    slli    a6, a5, 2
    bgt     a6, a4, 5f                  // fewer than 3 stages left
    mv      a2, a0
3:
    add     a3, a2, a5
    add     t5, a3, a5
    add     t6, t5, a5
    srli    t4, a5, 4
4:
    esp.vld.128.ip  q0, a2, 0
    esp.vld.128.ip  q1, a3, 0
    esp.vld.128.ip  q2, t5, 0
    esp.vld.128.ip  q3, t6, 0
    esp.vadd.s32    q4, q0, q1
    esp.vsub.s32    q1, q0, q1
    esp.vadd.s32    q0, q2, q3
    esp.vsub.s32    q3, q2, q3
    esp.vadd.s32    q2, q4, q0
    esp.vsub.s32    q4, q4, q0
    esp.vadd.s32    q0, q1, q3
    esp.vsub.s32    q1, q1, q3
    esp.vst.128.ip  q2, a2, 16
    esp.vst.128.ip  q0, a3, 16
    esp.vst.128.ip  q4, t5, 16
    esp.vst.128.ip  q1, t6, 16
    addi    t4, t4, -1
    bnez    t4, 4b
    mv      a2, t6
    blt     a2, t3, 3b
    slli    a5, a5, 2
    j       2b
5:
    esp.zero.q      q6
    esp.zero.q      q7
    mv      a2, a0
    add     a3, a0, a5
    beq     a5, a4, 6f                  // one stage left
    add     t5, a3, a5                  // two left: one radix-4 block over all of r
    add     t6, t5, a5
    srli    t4, a5, 4
1:
    esp.vld.128.ip  q0, a2, 0
    esp.vld.128.ip  q1, a3, 0
    esp.vld.128.ip  q2, t5, 0
    esp.vld.128.ip  q3, t6, 0
    esp.vadd.s32    q4, q0, q1
    esp.vsub.s32    q1, q0, q1
    esp.vadd.s32    q0, q2, q3
    esp.vsub.s32    q3, q2, q3
    esp.vadd.s32    q2, q4, q0
    esp.vsub.s32    q4, q4, q0
    esp.vadd.s32    q0, q1, q3
    esp.vsub.s32    q1, q1, q3
    esp.vmax.s32    q6, q6, q2
    esp.vmin.s32    q7, q7, q2
    esp.vst.128.ip  q2, a2, 16
    esp.vmax.s32    q6, q6, q0
    esp.vmin.s32    q7, q7, q0
    esp.vst.128.ip  q0, a3, 16
    esp.vmax.s32    q6, q6, q4
    esp.vmin.s32    q7, q7, q4
    esp.vst.128.ip  q4, t5, 16
    esp.vmax.s32    q6, q6, q1
    esp.vmin.s32    q7, q7, q1
    esp.vst.128.ip  q1, t6, 16
    addi    t4, t4, -1
    bnez    t4, 1b
    j       10f
6:
    srli    t4, a1, 3
7:
    esp.vld.128.ip  q0, a2, 0
    esp.vld.128.ip  q1, a3, 0
    esp.vadd.s32    q2, q0, q1
    esp.vsub.s32    q3, q0, q1
    esp.vmax.s32    q6, q6, q2
    esp.vmin.s32    q7, q7, q2
    esp.vmax.s32    q6, q6, q3
    esp.vmin.s32    q7, q7, q3
    esp.vst.128.ip  q2, a2, 16
    esp.vst.128.ip  q3, a3, 16
    addi    t4, t4, -1
    bnez    t4, 7b
10:
    addi    sp, sp, -32
    mv      a2, sp
    esp.vst.128.ip  q6, a2, 16
    esp.vst.128.ip  q7, a2, 16
    li      a0, 0                       // max of the 4 max lanes and the negated 4 min lanes (no Zbb)
    mv      a2, sp
    li      a3, 4
7:
    lw      a1, 0(a2)
    bge     a0, a1, 8f
    mv      a0, a1
8:
    lw      a1, 16(a2)
    neg     a1, a1
    bge     a0, a1, 9f
    mv      a0, a1
9:
    addi    a2, a2, 4
    addi    a3, a3, -1
    bnez    a3, 7b
    addi    sp, sp, 32
    ret
