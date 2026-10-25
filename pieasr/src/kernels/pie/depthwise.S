// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The depthwise kernels on PIE: the conv module's int8 depthwise convolution and its combine. C reference:
// ../c/depthwise.c.
#include "macros.inc"

    .text

// void pq_dw16(const int8_t *x, int ostep, int T, const int32_t *off, const int8_t *w, int ntaps, int32_t *acc)
// acc[t*16 + i] = sum_k w[k*16 + i] * x[t*ostep + off[k] + i]: lane-wise int8 MACs into QACC (16 x 32-bit lanes)
// a0 x (+ ostep per frame), a1 ostep, a2 T, a3 off, a4 w, a5 ntaps, a6 acc.
// PIE address registers: s0 walks w, s1 walks acc, a4 holds the tap address (w base kept in t3).
FUNC pq_dw16
    addi    sp, sp, -16
    sw      s0, 0(sp)
    sw      s1, 4(sp)
    mv      t3, a4                      // w base
    mv      s1, a6                      // acc
.Ld_frame:
    esp.zero.qacc
    mv      s0, t3
    mv      t4, a3                      // off pointer
    mv      t5, a5                      // taps left
.Ld_tap:
    lw      t0, 0(t4)
    addi    t4, t4, 4
    add     a4, a0, t0
    esp.vld.128.ip  q0, a4, 0           // x tap
    esp.vld.128.ip  q1, s0, 16          // its weights
    addi    t5, t5, -1
    esp.vmulas.s8.qacc q0, q1
    bnez    t5, .Ld_tap
    ST_QACC s1
    add     a0, a0, a1
    addi    a2, a2, -1
    bnez    a2, .Ld_frame
    lw      s0, 0(sp)
    lw      s1, 4(sp)
    addi    sp, sp, 16
    ret

// void pq_dwcomb16(const int32_t *accc, const int32_t *acck, int T, const pq_comb_t *P, int r, int sh,
//                  const int16_t *es, const uint32_t *tab, int16_t *y, int ldy)        (y, ldy on the stack)
// P: int16 Mc[16] at 0, Mk[16] at 32, int32 Bc[16] at 64, Bk[16] at 128 (16-byte aligned). Per frame, 16 lanes:
// yc, yk as in pq_out_act8 (vsr/vsat/vunzip, vmul.s32.s16xs16, vadd.s32, vsr/vsat/vunzip); yc parked on the stack;
// w = vmul.s16 (yk, es) at SAR 11, CFG[7:4] = 4; v = vadd.s16 (yc, w); h = the table (as pq_out_tab16); 2 stores.
TAB_CONSTS tab_c6, 63
FUNC pq_dwcomb16
    addi    sp, sp, -80
    sw      s0, 0(sp)
    sw      s1, 4(sp)
    sw      s2, 8(sp)
    sw      s3, 12(sp)
    sw      s4, 16(sp)
    sw      s5, 20(sp)
    sw      s6, 24(sp)
    sw      s10, 28(sp)
    lw      t6, 80(sp)                  // y
    lw      t0, 84(sp)                  // ldy
    slli    t0, t0, 1
    addi    t0, t0, -16
    mv      s6, a2                      // T (counter)
    mv      a2, t0                      // row step (a PIE address operand)
    mv      s2, a4                      // r
    mv      s3, a5                      // sh
    mv      t5, a6                      // es
    mv      s1, a7                      // tab
    addi    s10, sp, 48                 // yc parking (16-byte aligned: sp is)
    esp.movx.r.cfg  s0
    sw      s0, 32(sp)
    andi    s4, s0, -241                // CFG[7:4] = 0
    ori     s5, s4, 0x40                // CFG[7:4] = 4
    mv      s0, s4
    esp.movx.w.cfg  s0
    la      a5, tab_c6
    li      t3, -32768
    li      t4, 32767
1:
    // yc from accc (Mc at P, Bc at P + 64)
    mv      s0, s2
    esp.movx.w.sar  s0
    esp.vld.128.ip  q0, a0, 16
    esp.vld.128.ip  q1, a0, 16
    esp.vld.128.ip  q2, a0, 16
    esp.vld.128.ip  q3, a0, 16
    SR_SAT16 q0, q1, q2, q3
    li      s0, 0
    esp.movx.w.sar  s0
    mv      s0, a3
    esp.vld.128.ip  q4, s0, 16
    esp.vmul.s32.s16xs16    q5, q6, q0, q4
    esp.vld.128.ip  q4, s0, 48          // -> Bc
    esp.vmul.s32.s16xs16    q1, q3, q2, q4
    esp.vld.128.ip  q4, s0, 16
    esp.vadd.s32    q5, q5, q4
    esp.vld.128.ip  q4, s0, 16
    esp.vadd.s32    q6, q6, q4
    esp.vld.128.ip  q4, s0, 16
    esp.vadd.s32    q1, q1, q4
    esp.vld.128.ip  q4, s0, 0
    esp.vadd.s32    q3, q3, q4
    mv      s0, s3
    esp.movx.w.sar  s0
    SR_SAT16 q5, q6, q1, q3
    mv      s0, s10
    esp.vst.128.ip  q5, s0, 16
    esp.vst.128.ip  q1, s0, 0
    // yk from acck (Mk at P + 32, Bk at P + 128)
    mv      s0, s2
    esp.movx.w.sar  s0
    esp.vld.128.ip  q0, a1, 16
    esp.vld.128.ip  q1, a1, 16
    esp.vld.128.ip  q2, a1, 16
    esp.vld.128.ip  q3, a1, 16
    SR_SAT16 q0, q1, q2, q3
    li      s0, 0
    esp.movx.w.sar  s0
    addi    s0, a3, 32
    esp.vld.128.ip  q4, s0, 16
    esp.vmul.s32.s16xs16    q5, q6, q0, q4
    esp.vld.128.ip  q4, s0, 80          // -> Bk
    esp.vmul.s32.s16xs16    q1, q3, q2, q4
    esp.vld.128.ip  q4, s0, 16
    esp.vadd.s32    q5, q5, q4
    esp.vld.128.ip  q4, s0, 16
    esp.vadd.s32    q6, q6, q4
    esp.vld.128.ip  q4, s0, 16
    esp.vadd.s32    q1, q1, q4
    esp.vld.128.ip  q4, s0, 0
    esp.vadd.s32    q3, q3, q4
    mv      s0, s3
    esp.movx.w.sar  s0
    SR_SAT16 q5, q6, q1, q3             // yk 0..7 in q5, 8..15 in q1
    // v = yc + round(yk es / 2^11)
    li      s0, 11
    esp.movx.w.sar  s0
    mv      s0, s5
    esp.movx.w.cfg  s0
    esp.vld.128.ip  q4, t5, 16
    esp.vmul.s16    q5, q5, q4
    esp.vld.128.ip  q4, t5, 16
    esp.vmul.s16    q1, q1, q4
    mv      s0, s4
    esp.movx.w.cfg  s0
    mv      s0, s10
    esp.vld.128.ip  q4, s0, 16
    esp.vadd.s16    q5, q5, q4
    esp.vld.128.ip  q4, s0, 0
    esp.vadd.s16    q1, q1, q4
    // the table (SAR 6, floor)
    li      s0, 6
    esp.movx.w.sar  s0
    TAB8 q5
    TAB8 q1
    esp.vst.128.ip  q5, t6, 16
    esp.vst.128.xp  q1, t6, a2
    addi    s6, s6, -1
    bnez    s6, 1b
    lw      s0, 32(sp)
    esp.movx.w.cfg  s0
    lw      s0, 0(sp)
    lw      s1, 4(sp)
    lw      s2, 8(sp)
    lw      s3, 12(sp)
    lw      s4, 16(sp)
    lw      s5, 20(sp)
    lw      s6, 24(sp)
    lw      s10, 28(sp)
    addi    sp, sp, 80
    ret
