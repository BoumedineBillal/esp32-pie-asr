// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// Assembler macros shared by the PIE kernels.
// PIE address operands: x8..x15 or x24..x31 (s0, s1, a0..a5, s8..s11, t3..t6); esp.movx operands: x8..x15 only.

// a global function, 16-byte aligned
.macro FUNC name
    .global \name
    .type   \name, @function
    .align  4
\name:
.endm

// QACC (16 x 32-bit lanes) to / from 64 bytes at \p (post-incremented)
.macro ST_QACC p
    esp.st.qacc.l.l.128.ip  \p, 16
    esp.st.qacc.l.h.128.ip  \p, 16
    esp.st.qacc.h.l.128.ip  \p, 16
    esp.st.qacc.h.h.128.ip  \p, 16
.endm

.macro LD_QACC p
    esp.ld.qacc.l.l.128.ip  \p, 16
    esp.ld.qacc.l.h.128.ip  \p, 16
    esp.ld.qacc.h.l.128.ip  \p, 16
    esp.ld.qacc.h.h.128.ip  \p, 16
.endm

// 16 int8 MAC steps into QACC: the weight vectors alternate between q0 (even k) and q1 (odd k), each one loaded
// from s0 two instructions before its use; byte k of \x is the input of step k
.macro MAC16 x
    esp.vsmulas.s8.qacc.ld.incp q0, s0, q0, \x, 0
    esp.vsmulas.s8.qacc.ld.incp q1, s0, q1, \x, 1
    esp.vsmulas.s8.qacc.ld.incp q0, s0, q0, \x, 2
    esp.vsmulas.s8.qacc.ld.incp q1, s0, q1, \x, 3
    esp.vsmulas.s8.qacc.ld.incp q0, s0, q0, \x, 4
    esp.vsmulas.s8.qacc.ld.incp q1, s0, q1, \x, 5
    esp.vsmulas.s8.qacc.ld.incp q0, s0, q0, \x, 6
    esp.vsmulas.s8.qacc.ld.incp q1, s0, q1, \x, 7
    esp.vsmulas.s8.qacc.ld.incp q0, s0, q0, \x, 8
    esp.vsmulas.s8.qacc.ld.incp q1, s0, q1, \x, 9
    esp.vsmulas.s8.qacc.ld.incp q0, s0, q0, \x, 10
    esp.vsmulas.s8.qacc.ld.incp q1, s0, q1, \x, 11
    esp.vsmulas.s8.qacc.ld.incp q0, s0, q0, \x, 12
    esp.vsmulas.s8.qacc.ld.incp q1, s0, q1, \x, 13
    esp.vsmulas.s8.qacc.ld.incp q0, s0, q0, \x, 14
    esp.vsmulas.s8.qacc.ld.incp q1, s0, q1, \x, 15
.endm

// 8 gathers of 32-bit table entries at \base + 4 idx, idx the u16 lanes 0..7 of \idx: lanes 0-3 into \lo, 4-7 into \hi
.macro GATHER8 lo, hi, idx, base
    esp.ldxq.32     \lo, \idx, \base, 0, 0
    esp.ldxq.32     \lo, \idx, \base, 1, 1
    esp.ldxq.32     \lo, \idx, \base, 2, 2
    esp.ldxq.32     \lo, \idx, \base, 3, 3
    esp.ldxq.32     \hi, \idx, \base, 0, 4
    esp.ldxq.32     \hi, \idx, \base, 1, 5
    esp.ldxq.32     \hi, \idx, \base, 2, 6
    esp.ldxq.32     \hi, \idx, \base, 3, 7
.endm

// 16 int32 lanes (\a, \b, \c, \d) >> SAR, saturated to [t3, t4], packed as int16: lanes 0-7 in \a, 8-15 in \c
.macro SR_SAT16 a, b, c, d
    esp.vsr.s32     \a, \a
    esp.vsr.s32     \b, \b
    esp.vsr.s32     \c, \c
    esp.vsr.s32     \d, \d
    esp.vsat.s32    \a, \a, t3, t4
    esp.vsat.s32    \b, \b, t3, t4
    esp.vsat.s32    \c, \c, t3, t4
    esp.vsat.s32    \d, \d, t3, t4
    esp.vunzip.16   \a, \b
    esp.vunzip.16   \c, \d
.endm

// the activation table on 8 int16 lanes of \v, in place (kernels.h pq_tab_eval_k, k = SAR): the lane constants at a5
// (TAB_CONSTS), the table at s1; e = tab[(v + 32768) >> k], h = (int16)e + (((e >> 16) * (v & mask)) >> k).
// Uses s0, q0, q2, q3, q4, q6, q7; SAR = k and CFG[7:4] = 0 (floor) set by the caller.
.macro TAB8 v
    mv      s0, a5
    esp.vld.128.ip  q7, s0, 16          // mask
    esp.andq        q4, \v, q7          // fraction
    esp.vld.128.ip  q6, s0, 16          // 1
    esp.vld.128.ip  q7, s0, 0           // 0x8000
    esp.xorq        q0, \v, q7
    esp.vmul.u16    q0, q0, q6          // index
    GATHER8 q2, q3, q0, s1
    esp.vunzip.16   q2, q3              // f, d
    esp.vmul.s16    q3, q3, q4
    esp.vadd.s16    \v, q2, q3
.endm

// the table lookup's lane constants (8 x u16 each): the fraction mask (2^k - 1), 1 (vmul.u16 by 1 at SAR k: a logical
// shift), 0x8000 (xor: v + 32768 as u16)
.macro TAB_CONSTS label, mask
    .section .rodata
    .align  4
\label:
    .short  \mask, \mask, \mask, \mask, \mask, \mask, \mask, \mask
    .short  1, 1, 1, 1, 1, 1, 1, 1
    .short  0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000
    .text
.endm
