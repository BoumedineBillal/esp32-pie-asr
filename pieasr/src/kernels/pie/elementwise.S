// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The elementwise kernels on PIE: int32 to int8 by a shift, int16 products, per-lane scales. C reference:
// ../c/elementwise.c.
#include "macros.inc"

    .text

// void pq_q8_shift(const int32_t *r, int n, int e, int8_t *q)
// q[k] = sat8(round_half_even(r[k] / 2^e)), k < n (a multiple of 16); r and q 16-byte aligned.
// 16 values per step: 4 loads into QACC, one srcmb.s8.qacc (shift e, rounding by CFG[7:4] = 6, saturation).
// CFG is restored.
FUNC pq_q8_shift
    esp.movx.r.cfg  a4
    mv      a5, a4
    andi    a4, a4, -241
    ori     a4, a4, 0x60                // CFG[7:4] = 6: nearest, ties to even
    esp.movx.w.cfg  a4
    srli    a1, a1, 4
1:
    LD_QACC a0
    esp.srcmb.s8.qacc   q0, a2, 1
    esp.vst.128.ip  q0, a3, 16
    addi    a1, a1, -1
    bnez    a1, 1b
    esp.movx.w.cfg  a5
    ret

// void pq_mul_q8(const int16_t *a, const int16_t *b, int lda, int T, const int16_t *N, int sh2, int8_t *y, int ldy)
// per frame, 16 lanes: g = vmul.s16 (a, b) at SAR 15; q = vmul.s16 (g, N) at SAR sh2 (CFG[7:4] = 4: nearest, ties
// away, both); vsat.s16 [-127, 127]; vunzip.8 -> 16 bytes. CFG restored.
FUNC pq_mul_q8
    addi    sp, sp, -16
    sw      s0, 0(sp)
    sw      s1, 4(sp)
    esp.movx.r.cfg  s0
    sw      s0, 8(sp)
    andi    s0, s0, -241
    ori     s0, s0, 0x40
    esp.movx.w.cfg  s0
    esp.vld.128.ip  q6, a4, 16          // N 0..7
    esp.vld.128.ip  q7, a4, 0           // N 8..15
    slli    a2, a2, 1
    addi    a2, a2, -16                 // row step after the first load
    mv      t5, a6
    mv      t6, a7
    li      s0, 15
    mv      s1, a5
    li      t3, -127
    li      t4, 127
1:
    esp.movx.w.sar  s0
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.xp  q1, a0, a2
    esp.vld.128.ip  q2, a1, 16
    esp.vld.128.xp  q3, a1, a2
    esp.vmul.s16    q0, q0, q2
    esp.vmul.s16    q1, q1, q3
    esp.movx.w.sar  s1
    esp.vmul.s16    q0, q0, q6
    esp.vmul.s16    q1, q1, q7
    esp.vsat.s16    q0, q0, t3, t4
    esp.vsat.s16    q1, q1, t3, t4
    esp.vunzip.8    q0, q1
    esp.vst.128.xp  q0, t5, t6
    addi    a3, a3, -1
    bnez    a3, 1b
    lw      s0, 8(sp)
    esp.movx.w.cfg  s0
    lw      s0, 0(sp)
    lw      s1, 4(sp)
    addi    sp, sp, 16
    ret

// void pq_mul16(const int16_t *a, const int16_t *b, int lda, int T, int16_t *y)
// y[t*16 + i] = vmul.s16 (a, b) at SAR 15, CFG[7:4] = 4 (nearest, ties away), 16 lanes per frame. CFG restored.
FUNC pq_mul16
    addi    sp, sp, -16
    sw      s0, 0(sp)
    esp.movx.r.cfg  s0
    sw      s0, 4(sp)
    andi    s0, s0, -241
    ori     s0, s0, 0x40
    esp.movx.w.cfg  s0
    li      s0, 15
    esp.movx.w.sar  s0
    slli    a2, a2, 1
    addi    a2, a2, -16                 // row step after the first load
1:
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.xp  q1, a0, a2
    esp.vld.128.ip  q2, a1, 16
    esp.vld.128.xp  q3, a1, a2
    esp.vmul.s16    q0, q0, q2
    esp.vmul.s16    q1, q1, q3
    esp.vst.128.ip  q0, a4, 16
    esp.vst.128.ip  q1, a4, 16
    addi    a3, a3, -1
    bnez    a3, 1b
    lw      s0, 4(sp)
    esp.movx.w.cfg  s0
    lw      s0, 0(sp)
    addi    sp, sp, 16
    ret

// void pq_lane_q8(const int16_t *g, int T, const int16_t *N, int8_t *y, int ldy)
// y[t*ldy + i] = vsat.s16 [-127, 127] of vmul.s16 (g, N) at SAR 15, CFG[7:4] = 4; vunzip.8 -> 16 bytes per frame.
FUNC pq_lane_q8
    addi    sp, sp, -16
    sw      s0, 0(sp)
    esp.movx.r.cfg  s0
    sw      s0, 4(sp)
    andi    s0, s0, -241
    ori     s0, s0, 0x40
    esp.movx.w.cfg  s0
    li      s0, 15
    esp.movx.w.sar  s0
    esp.vld.128.ip  q6, a2, 16          // N 0..7
    esp.vld.128.ip  q7, a2, 0           // N 8..15
    li      t3, -127
    li      t4, 127
1:
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.ip  q1, a0, 16
    esp.vmul.s16    q0, q0, q6
    esp.vmul.s16    q1, q1, q7
    esp.vsat.s16    q0, q0, t3, t4
    esp.vsat.s16    q1, q1, t3, t4
    esp.vunzip.8    q0, q1
    esp.vst.128.xp  q0, a3, a4
    addi    a1, a1, -1
    bnez    a1, 1b
    lw      s0, 4(sp)
    esp.movx.w.cfg  s0
    lw      s0, 0(sp)
    addi    sp, sp, 16
    ret

// void pq_lane_or16(const int16_t *g, int T, int32_t *m)
// m[i] = OR over the rows of (g ^ (g >> 15)) (16 lanes): x ^ (x < 0 ? -1 : 0) via vcmp.lt.s16 against zero (mask)
// and xorq (no 16-bit arithmetic shift is used); then orq into two accumulators; widened to 32 bits at the end
// (vzip.16 with zeros).
FUNC pq_lane_or16
    esp.zero.q      q6
    esp.zero.q      q7
    esp.zero.q      q5
1:
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.ip  q1, a0, 16
    esp.vcmp.lt.s16 q2, q0, q5
    esp.vcmp.lt.s16 q3, q1, q5
    esp.xorq        q0, q0, q2
    esp.xorq        q1, q1, q3
    esp.orq         q6, q6, q0
    esp.orq         q7, q7, q1
    addi    a1, a1, -1
    bnez    a1, 1b
    esp.zero.q      q5
    esp.vzip.16     q6, q5              // lanes 0..7 as u32: q6 = 0..3, q5 = 4..7 (high halves zero)
    esp.zero.q      q4
    esp.vzip.16     q7, q4              // lanes 8..15
    esp.vst.128.ip  q6, a2, 16
    esp.vst.128.ip  q5, a2, 16
    esp.vst.128.ip  q7, a2, 16
    esp.vst.128.ip  q4, a2, 16
    ret
