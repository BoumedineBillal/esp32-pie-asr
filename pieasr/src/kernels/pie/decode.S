// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The decode kernels on PIE: 2/4-bit weight codes to int8 levels through a table. C reference: ../c/decode.c.
#include "macros.inc"

    .text

// void pq_decode2(const uint8_t *codes, int nbytes, const uint32_t *lut, int8_t *out)
// nbytes a multiple of 16; each code byte b -> the 4 bytes of lut[b]
FUNC pq_decode2
    srli    a1, a1, 4                   // blocks of 16 code bytes
1:
    esp.vld.128.ip  q4, a0, 16          // 16 code bytes
    esp.zero.q      q5
    esp.vzip.8      q4, q5              // q4: bytes 0..7 as u16 lanes, q5: bytes 8..15
    nop
    GATHER8 q0, q1, q4, a2
    GATHER8 q2, q3, q5, a2
    esp.vst.128.ip  q0, a3, 16
    esp.vst.128.ip  q1, a3, 16
    esp.vst.128.ip  q2, a3, 16
    esp.vst.128.ip  q3, a3, 16
    addi    a1, a1, -1
    bnez    a1, 1b
    ret

// void pq_decode4(const uint8_t *codes, int nbytes, const uint32_t *lut, int8_t *out)
// nbytes a multiple of 16; each code byte b -> the 2 low bytes of lut[b]
FUNC pq_decode4
    srli    a1, a1, 4
1:
    esp.vld.128.ip  q4, a0, 16
    esp.zero.q      q5
    esp.vzip.8      q4, q5
    nop
    GATHER8 q0, q1, q4, a2
    GATHER8 q2, q3, q5, a2
    esp.vunzip.16   q0, q1              // q0: the 16 levels of code bytes 0..7
    esp.vunzip.16   q2, q3              // q2: code bytes 8..15
    esp.vst.128.ip  q0, a3, 16
    esp.vst.128.ip  q2, a3, 16
    addi    a1, a1, -1
    bnez    a1, 1b
    ret

// pq_decode2 with the 16 gathers interleaved over the 4 destination registers (no C version: the same results)
FUNC pq_decode2_il
    srli    a1, a1, 4
1:
    esp.vld.128.ip  q4, a0, 16
    esp.zero.q      q5
    esp.vzip.8      q4, q5
    nop
    esp.ldxq.32     q0, q4, a2, 0, 0
    esp.ldxq.32     q1, q4, a2, 0, 4
    esp.ldxq.32     q2, q5, a2, 0, 0
    esp.ldxq.32     q3, q5, a2, 0, 4
    esp.ldxq.32     q0, q4, a2, 1, 1
    esp.ldxq.32     q1, q4, a2, 1, 5
    esp.ldxq.32     q2, q5, a2, 1, 1
    esp.ldxq.32     q3, q5, a2, 1, 5
    esp.ldxq.32     q0, q4, a2, 2, 2
    esp.ldxq.32     q1, q4, a2, 2, 6
    esp.ldxq.32     q2, q5, a2, 2, 2
    esp.ldxq.32     q3, q5, a2, 2, 6
    esp.ldxq.32     q0, q4, a2, 3, 3
    esp.ldxq.32     q1, q4, a2, 3, 7
    esp.ldxq.32     q2, q5, a2, 3, 3
    esp.ldxq.32     q3, q5, a2, 3, 7
    esp.vst.128.ip  q0, a3, 16
    esp.vst.128.ip  q1, a3, 16
    esp.vst.128.ip  q2, a3, 16
    esp.vst.128.ip  q3, a3, 16
    addi    a1, a1, -1
    bnez    a1, 1b
    ret
