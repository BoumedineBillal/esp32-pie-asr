// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The tile kernels on PIE: 16 output rows x G int8 inputs into int32 sums. C reference: ../c/tile.c.
#include "macros.inc"

    .text

// void pq_tile16(const int8_t *w, const int8_t *x, int xstride, int T, int G, int32_t *acc)
// acc[t*16 + i] = sum_k w[k*16 + i] * x[t*xstride + k]; G a multiple of 16. Reads up to 32 bytes past the end of
// w and 16 past each x row (the scratch has the slack).
// Weight vectors alternate between q0 (even k) and q1 (odd k), each loaded 2 instructions before its use; the
// inputs alternate between q2 and q3 per 16 k. The walking pointers are s0 (weights) and s1 (inputs), PIE address
// registers, saved on the stack.
FUNC pq_tile16
    addi    sp, sp, -16
    sw      s0, 0(sp)
    sw      s1, 4(sp)
    srli    a4, a4, 4                   // blocks of 16 k
.Lt_frame:
    mv      s0, a0
    mv      s1, a1
    mv      t2, a4
    esp.zero.qacc
    esp.vld.128.ip  q0, s0, 16
    esp.vld.128.ip  q1, s0, 16
    esp.vld.128.ip  q2, s1, 16
.Lt_block:
    esp.vld.128.ip  q3, s1, 16          // the next 16 inputs
    MAC16 q2
    addi    t2, t2, -1
    beqz    t2, .Lt_done
    esp.vld.128.ip  q2, s1, 16
    MAC16 q3
    addi    t2, t2, -1
    bnez    t2, .Lt_block
.Lt_done:
    ST_QACC a5
    add     a1, a1, a2
    addi    a3, a3, -1
    bnez    a3, .Lt_frame
    lw      s0, 0(sp)
    lw      s1, 4(sp)
    addi    sp, sp, 16
    ret

// void pq_tile16_n(const int8_t *w, const int8_t *x, int xstride, int T, int G, const int16_t *n16,
//                  const uint8_t *sar, int32_t *y32)
// The integer product's tile (kernels.h): for t < T, i < 16:
//   acc = 32 + sum_k w[k*16 + i] x[t*xstride + k];  y32[t*16 + i] = sat32(y32 + (((acc >> 6) * n16[i]) >> sar[t]))
// QACC starts at 32 (4 loads from c32); read-out acc >> 6 as int16 = srcmb.s8 (raw low byte, QACC := acc >> 6)
// + srcmb.s8 (>> 8, saturated high byte) + vzip.8 (exact at CFG[7:4] = 0); then vmul.s32.s16xs16 by the norms
// (>> SAR = sar[t], floor) and vadd.s32 into y32. CFG[7:4] is set to 0 (floor) and restored.
// Registers: q6, q7 = n16; q0-q3 = the MAC loop, then the products; q4, q5 = read-out, then y32.
// a4 = c32 (QACC start), a5 = y32 (walking), a6 = sar; t5 = blocks of 16 k; t3 = 6, t4 = 8.
    .section .rodata
    .align  4
c32:
    .word   32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32
    .text
FUNC pq_tile16_n
    addi    sp, sp, -16
    sw      s0, 0(sp)
    sw      s1, 4(sp)
    esp.movx.r.cfg  s0
    sw      s0, 8(sp)
    andi    s0, s0, -241                // CFG[7:4] = 0: floor
    esp.movx.w.cfg  s0
    esp.vld.128.ip  q6, a5, 16
    esp.vld.128.ip  q7, a5, 16
    mv      a5, a7
    srli    t5, a4, 4
    la      a4, c32
    li      t3, 6
    li      t4, 8
.Ln_frame:
    mv      s0, a0
    mv      s1, a1
    mv      t2, t5
    LD_QACC a4
    addi    a4, a4, -64
    esp.vld.128.ip  q0, s0, 16
    esp.vld.128.ip  q1, s0, 16
    esp.vld.128.ip  q2, s1, 16
.Ln_block:
    esp.vld.128.ip  q3, s1, 16
    MAC16 q2
    addi    t2, t2, -1
    beqz    t2, .Ln_done
    esp.vld.128.ip  q2, s1, 16
    MAC16 q3
    addi    t2, t2, -1
    bnez    t2, .Ln_block
.Ln_done:
    esp.srcmb.s8.qacc   q4, t3, 0       // low bytes of acc >> 6; QACC := acc >> 6
    esp.srcmb.s8.qacc   q5, t4, 1       // sat8((acc >> 6) >> 8)
    lbu     s0, 0(a6)                   // (s0 is free until the next frame)
    addi    a6, a6, 1
    esp.vzip.8  q4, q5                  // int16: lanes 0-7 in q4, 8-15 in q5
    esp.movx.w.sar  s0
    esp.vmul.s32.s16xs16    q0, q1, q4, q6  // rows 0-3 -> q0, 4-7 -> q1
    esp.vmul.s32.s16xs16    q2, q3, q5, q7  // rows 8-11 -> q2, 12-15 -> q3
    esp.vld.128.ip  q4, a5, 16
    esp.vld.128.ip  q5, a5, 16
    esp.vadd.s32    q4, q4, q0
    esp.vadd.s32    q5, q5, q1
    addi    a5, a5, -32
    esp.vst.128.ip  q4, a5, 16
    esp.vst.128.ip  q5, a5, 16
    esp.vld.128.ip  q4, a5, 16
    esp.vld.128.ip  q5, a5, 16
    esp.vadd.s32    q4, q4, q2
    esp.vadd.s32    q5, q5, q3
    addi    a5, a5, -32
    esp.vst.128.ip  q4, a5, 16
    esp.vst.128.ip  q5, a5, 16
    add     a1, a1, a2
    addi    a3, a3, -1
    bnez    a3, .Ln_frame
    lw      s0, 8(sp)
    esp.movx.w.cfg  s0
    lw      s0, 0(sp)
    lw      s1, 4(sp)
    addi    sp, sp, 16
    ret
