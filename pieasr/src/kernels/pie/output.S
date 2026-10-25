// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The output kernels on PIE: int32 tiles to int16 through activation tables or into the int16 residual, to int8;
// the int16 BiasNorm's sum of squares and bypass. C reference: ../c/output.c.
#include "macros.inc"

    .text

// void pq_out_tab16(const int32_t *y32, int T, const uint32_t *tab, int16_t *h, int ldh)
// h[t*ldh + i] = activation table of v = sat16(y32[t*16 + i] >> 8), t < T, i < 16 (kernels.h, pq_tab_eval):
//   e = tab[(v + 32768) >> 6], h = (int16)e + (((e >> 16) * (v & 63)) >> 6)       (floor)
// y32, h 16-byte aligned, ldh a multiple of 8. CFG[7:4] = 0 (floor) for vmul, restored; SAR switched 8 / 6.
// q5 = 63 (mask), q6 = 1 (vmul.u16: logical >> SAR), q7 = 0x8000 (xor: v + 32768 as u16).
TAB_CONSTS tab_c6, 63
TAB_CONSTS tab_c4, 15
FUNC pq_out_tab16
    addi    sp, sp, -16
    sw      s0, 0(sp)
    sw      s1, 4(sp)
    esp.movx.r.cfg  s0
    sw      s0, 8(sp)
    andi    s0, s0, -241
    esp.movx.w.cfg  s0
    la      s0, tab_c6
    esp.vld.128.ip  q5, s0, 16
    esp.vld.128.ip  q6, s0, 16
    esp.vld.128.ip  q7, s0, 16
    slli    a4, a4, 1                   // ldh in bytes
    addi    a4, a4, -16                 // (after the first of the two stores)
    li      t3, -32768
    li      t4, 32767
    li      s0, 8
    li      s1, 6
    beqz    a2, 20f                     // no table: the identity
1:
    esp.movx.w.sar  s0
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.ip  q1, a0, 16
    esp.vld.128.ip  q2, a0, 16
    esp.vld.128.ip  q3, a0, 16
    SR_SAT16 q0, q1, q2, q3             // q0 = v 0..7, q2 = v 8..15
    esp.movx.w.sar  s1
    esp.xorq        q1, q0, q7
    esp.vmul.u16    q1, q1, q6          // index
    esp.andq        q0, q0, q5          // fraction
    GATHER8 q3, q4, q1, a2
    esp.vunzip.16   q3, q4              // q3 = f, q4 = d
    esp.vmul.s16    q4, q4, q0
    esp.xorq        q1, q2, q7
    esp.vadd.s16    q3, q3, q4
    esp.vmul.u16    q1, q1, q6
    esp.andq        q2, q2, q5
    esp.vst.128.ip  q3, a3, 16
    GATHER8 q3, q4, q1, a2
    esp.vunzip.16   q3, q4
    esp.vmul.s16    q4, q4, q2
    esp.vadd.s16    q3, q3, q4
    esp.vst.128.xp  q3, a3, a4
    addi    a1, a1, -1
    bnez    a1, 1b
    j       21f
20:
    esp.movx.w.sar  s0
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.ip  q1, a0, 16
    esp.vld.128.ip  q2, a0, 16
    esp.vld.128.ip  q3, a0, 16
    SR_SAT16 q0, q1, q2, q3
    esp.vst.128.ip  q0, a3, 16
    esp.vst.128.xp  q2, a3, a4
    addi    a1, a1, -1
    bnez    a1, 20b
21:
    lw      s0, 8(sp)
    esp.movx.w.cfg  s0
    lw      s0, 0(sp)
    lw      s1, 4(sp)
    addi    sp, sp, 16
    ret

// void pq_out_add16(const int32_t *y32, int T, int n, int j, int16_t *x, int ldx)
// x[t*ldx + i] = sat16(sat32(x[t*ldx + i] * 2^(n-j) + y32[t*16 + i]) >> n), t < T, i < 16; 1 <= n - j <= 14.
// Per 8 values: vmul.s32.s16xs16 by 2^(n-j) (SAR 0: exact), vadd.s32 (saturating), vsr.s32 (SAR n), vsat.s32 to
// int16, vunzip.16. CFG[7:4] = 0 (floor) and restored. q7 = 2^(n-j) in 8 lanes.
FUNC pq_out_add16
    addi    sp, sp, -32
    sw      s0, 16(sp)
    sw      s1, 20(sp)
    esp.movx.r.cfg  s0
    sw      s0, 24(sp)
    andi    s0, s0, -241
    esp.movx.w.cfg  s0
    sub     t3, a2, a3
    li      t4, 1
    sll     t4, t4, t3                  // 2^(n-j)
    sh      t4, 0(sp)
    sh      t4, 2(sp)
    sh      t4, 4(sp)
    sh      t4, 6(sp)
    sh      t4, 8(sp)
    sh      t4, 10(sp)
    sh      t4, 12(sp)
    sh      t4, 14(sp)
    mv      s0, sp
    esp.vld.128.ip  q7, s0, 0
    slli    a5, a5, 1
    addi    a5, a5, -16                 // row step after the first store
    li      s0, 0
    mv      s1, a2
    li      t3, -32768
    li      t4, 32767
1:
    esp.movx.w.sar  s0
    esp.vld.128.ip  q2, a4, 16          // x 0..7
    esp.vld.128.ip  q5, a4, -16         // x 8..15
    esp.vmul.s32.s16xs16    q3, q4, q2, q7
    esp.vmul.s32.s16xs16    q2, q6, q5, q7
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.ip  q1, a0, 16
    esp.vadd.s32    q0, q0, q3
    esp.vadd.s32    q1, q1, q4
    esp.vld.128.ip  q3, a0, 16
    esp.vld.128.ip  q4, a0, 16
    esp.vadd.s32    q3, q3, q2
    esp.vadd.s32    q4, q4, q6
    esp.movx.w.sar  s1
    SR_SAT16 q0, q1, q3, q4
    esp.vst.128.ip  q0, a4, 16
    esp.vst.128.xp  q3, a4, a5
    addi    a1, a1, -1
    bnez    a1, 1b
    lw      s0, 24(sp)
    esp.movx.w.cfg  s0
    lw      s0, 16(sp)
    lw      s1, 20(sp)
    addi    sp, sp, 32
    ret

// void pq_out_act8(const int32_t *acc, int T, const int16_t *M, const int32_t *B, const int16_t *N, int r, int sh,
//                  int sh2, const uint32_t *tab, int8_t *y, int ldy)            (kernels.h; tab, y, ldy on the stack)
// per frame, 16 lanes: a = sat16(acc >> r) (vsr.s32, vsat.s32, vunzip.16); v = sat16(sat32(a M + B) >> sh)
// (vmul.s32.s16xs16 at SAR 0, vadd.s32, vsr.s32, vsat.s32, vunzip.16); h = table (as pq_out_tab16) or v;
// q = vmul.s16 (h, N) at SAR sh2 with CFG[7:4] = 4 (nearest, ties away), vsat.s16 [-127, 127], vunzip.8 -> 16 bytes.
// Scalars in s2..s7 (only moved into s0 for movx), vsat bounds t3/t4 and s8/s9, CFG restored.
FUNC pq_out_act8
    addi    sp, sp, -48
    sw      s0, 0(sp)
    sw      s1, 4(sp)
    sw      s2, 8(sp)
    sw      s3, 12(sp)
    sw      s4, 16(sp)
    sw      s5, 20(sp)
    sw      s6, 24(sp)
    sw      s7, 28(sp)
    sw      s8, 32(sp)
    sw      s9, 36(sp)
    lw      s1, 48(sp)                  // tab
    lw      t5, 52(sp)                  // y
    mv      s7, a1                      // T (counter)
    lw      a1, 56(sp)                  // ldy (a PIE address operand)
    mv      s2, a5
    mv      s3, a6
    mv      s4, a7
    esp.movx.r.cfg  s0
    sw      s0, 40(sp)
    andi    s5, s0, -241                // CFG[7:4] = 0
    ori     s6, s5, 0x40                // CFG[7:4] = 4
    mv      s0, s5
    esp.movx.w.cfg  s0
    la      a5, tab_c4                  // 15, 1, 0x8000 (8 lanes each): the 4096-entry table
    li      t3, -32768
    li      t4, 32767
    li      s8, -127
    li      s9, 127
1:
    mv      s0, s2
    esp.movx.w.sar  s0
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.ip  q1, a0, 16
    esp.vld.128.ip  q2, a0, 16
    esp.vld.128.ip  q3, a0, 16
    SR_SAT16 q0, q1, q2, q3             // a 0..7 in q0, 8..15 in q2
    li      s0, 0
    esp.movx.w.sar  s0
    esp.vld.128.ip  q4, a2, 16
    esp.vmul.s32.s16xs16    q5, q6, q0, q4
    esp.vld.128.ip  q4, a2, 16
    esp.vmul.s32.s16xs16    q1, q3, q2, q4
    esp.vld.128.ip  q4, a3, 16
    esp.vadd.s32    q5, q5, q4
    esp.vld.128.ip  q4, a3, 16
    esp.vadd.s32    q6, q6, q4
    esp.vld.128.ip  q4, a3, 16
    esp.vadd.s32    q1, q1, q4
    esp.vld.128.ip  q4, a3, 16
    esp.vadd.s32    q3, q3, q4
    mv      s0, s3
    esp.movx.w.sar  s0
    SR_SAT16 q5, q6, q1, q3             // v 0..7 in q5, 8..15 in q1
    beqz    s1, 2f
    li      s0, 4                       // PQ_ACT8_K
    esp.movx.w.sar  s0
    TAB8 q5                             // h 0..7
    TAB8 q1                             // h 8..15
2:
    mv      s0, s4
    esp.movx.w.sar  s0
    mv      s0, s6
    esp.movx.w.cfg  s0                  // nearest, ties away
    esp.vld.128.ip  q4, a4, 16
    esp.vmul.s16    q5, q5, q4
    esp.vld.128.ip  q4, a4, 16
    esp.vmul.s16    q1, q1, q4
    mv      s0, s5
    esp.movx.w.cfg  s0
    esp.vsat.s16    q5, q5, s8, s9
    esp.vsat.s16    q1, q1, s8, s9
    esp.vunzip.8    q5, q1              // the low bytes: 16 x int8
    esp.vst.128.xp  q5, t5, a1
    addi    a2, a2, -32
    addi    a3, a3, -64
    addi    a4, a4, -32
    addi    s7, s7, -1
    bnez    s7, 1b
    lw      s0, 40(sp)
    esp.movx.w.cfg  s0
    lw      s0, 0(sp)
    lw      s1, 4(sp)
    lw      s2, 8(sp)
    lw      s3, 12(sp)
    lw      s4, 16(sp)
    lw      s5, 20(sp)
    lw      s6, 24(sp)
    lw      s7, 28(sp)
    lw      s8, 32(sp)
    lw      s9, 36(sp)
    addi    sp, sp, 48
    ret

// uint32_t pq_sumsq16(const int16_t *x, const int16_t *b, int C)
// sum_c (sat16(x[c] - b[c])^2 >> 8): vsub.s16 (saturating), vmul.s32.s16xs16 (SAR 8, floor), vadd.s32 per lane.
FUNC pq_sumsq16
    addi    sp, sp, -32
    esp.movx.r.cfg  a3
    sw      a3, 16(sp)
    andi    a3, a3, -241
    esp.movx.w.cfg  a3
    li      a3, 8
    esp.movx.w.sar  a3
    esp.zero.q      q6
    esp.zero.q      q7
    srli    a2, a2, 3
1:
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.ip  q1, a1, 16
    esp.vsub.s16    q0, q0, q1
    esp.vmul.s32.s16xs16    q2, q3, q0, q0
    esp.vadd.s32    q6, q6, q2
    esp.vadd.s32    q7, q7, q3
    addi    a2, a2, -1
    bnez    a2, 1b
    esp.vadd.s32    q6, q6, q7
    mv      a3, sp
    esp.vst.128.ip  q6, a3, 0
    lw      a0, 0(sp)
    lw      a1, 4(sp)
    add     a0, a0, a1
    lw      a1, 8(sp)
    add     a0, a0, a1
    lw      a1, 12(sp)
    add     a0, a0, a1
    lw      a3, 16(sp)
    esp.movx.w.cfg  a3
    addi    sp, sp, 32
    ret

// void pq_scale_bypass16(const int16_t *x, const int16_t *xo, int C, int A, int k, const int16_t *bb,
//                        const int16_t *w, int16_t *out)
// n = sat16((x * A) >> k) (vmul.s16, SAR k, floor); out = sat16((xo * w + n * bb + 8192) >> 14) (vmul.s32.s16xs16
// at SAR 0, vadd.s32, vsr.s32 at SAR 14, vsat.s32, vunzip.16). C a multiple of 8. CFG[7:4] = 0, restored.
    .section .rodata
    .align  4
c8192:
    .word   8192, 8192, 8192, 8192
    .text
FUNC pq_scale_bypass16
    addi    sp, sp, -32
    sw      s0, 16(sp)
    sw      s1, 20(sp)
    esp.movx.r.cfg  s0
    sw      s0, 24(sp)
    andi    s0, s0, -241
    esp.movx.w.cfg  s0
    sh      a3, 0(sp)
    sh      a3, 2(sp)
    sh      a3, 4(sp)
    sh      a3, 6(sp)
    sh      a3, 8(sp)
    sh      a3, 10(sp)
    sh      a3, 12(sp)
    sh      a3, 14(sp)
    mv      s0, sp
    esp.vld.128.ip  q7, s0, 0           // A in 8 lanes
    la      s0, c8192
    esp.vld.128.ip  q6, s0, 0
    mv      t5, a6                      // (address operands: x8..x15, x24..x31)
    mv      t6, a7
    li      t3, -32768
    li      t4, 32767
    li      s0, 0
    li      s1, 14
    srli    a2, a2, 3
1:
    esp.movx.w.sar  a4
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.ip  q1, a1, 16
    esp.vld.128.ip  q2, a5, 16
    esp.vld.128.ip  q3, t5, 16
    esp.vmul.s16    q0, q0, q7          // n
    esp.movx.w.sar  s0
    esp.vmul.s32.s16xs16    q4, q5, q1, q3
    esp.vmul.s32.s16xs16    q1, q3, q0, q2
    esp.vadd.s32    q4, q4, q6
    esp.vadd.s32    q5, q5, q6
    esp.vadd.s32    q4, q4, q1
    esp.vadd.s32    q5, q5, q3
    esp.movx.w.sar  s1
    esp.vsr.s32     q4, q4
    esp.vsr.s32     q5, q5
    esp.vsat.s32    q4, q4, t3, t4
    esp.vsat.s32    q5, q5, t3, t4
    esp.vunzip.16   q4, q5
    esp.vst.128.ip  q4, t6, 16
    addi    a2, a2, -1
    bnez    a2, 1b
    lw      s0, 24(sp)
    esp.movx.w.cfg  s0
    lw      s0, 16(sp)
    lw      s1, 20(sp)
    addi    sp, sp, 32
    ret
