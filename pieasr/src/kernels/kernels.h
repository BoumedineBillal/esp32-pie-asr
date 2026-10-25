// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The kernels: each one in PIE assembly (pie/, the chip) and in C (c/, its bit-exact reference, the host). The
// component builds pie/*.S when PQ_HAVE_PIE is defined, the host build c/*.c.
#pragma once
#include <stddef.h>
#include <stdint.h>

#if defined(__riscv) && defined(PQ_HAVE_PIE)
#define PQ_PIE 1
#endif

// the cycle counter on the chip (mcycle), 0 elsewhere
static inline uint32_t pq_cycles(void)
{
#ifdef PQ_PIE
    uint32_t c;
    __asm__ volatile("csrr %0, mcycle" : "=r"(c));
    return c;
#else
    return 0;
#endif
}

// max of two floats (finite values): one fmax.s on RISC-V (gcc otherwise calls fmaxf), fmaxf elsewhere
static inline float pq_fmax(float a, float b)
{
#if defined(__riscv)
    float r;
    __asm__("fmax.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b));
    return r;
#else
    return __builtin_fmaxf(a, b);
#endif
}

// round to nearest, ties to even (= lrintf in the default rounding mode): one fcvt.w.s on RISC-V
static inline int pq_round(float v)
{
#if defined(__riscv)
    int r;
    __asm__("fcvt.w.s %0, %1, rne" : "=r"(r) : "f"(v));
    return r;
#else
    return (int)__builtin_lrintf(v);
#endif
}

static inline int32_t pq_sat16(int32_t v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

// round(v / 2^s), ties away from 0 (vmul.s16 at CFG[7:4] = 4)
static inline int32_t pq_rshift_away(int32_t v, int s)
{
    if (s <= 0) {
        return v;
    }
    const int32_t u = v < 0 ? -v : v, q = (u + (1 << (s - 1))) >> s;
    return v < 0 ? -q : q;
}

// ---- activation tables for an int16 product output v (value v * sy) -> int16 h (value h * sh), PQ_TAB_N entries:
//   e = tab[(v + 32768) >> 6],  h = (int16)e + (((e >> 16) * (v & 63)) >> 6)      (floor; e >> 16 signed)
// entry j: f_j | (f_{j+1} - f_j) << 16, f_j = sat16(round(f((64 j - 32768) sy) / sh)) (pq_tab_build)
#define PQ_TAB_N 1024
// a finer grid: 2^(16 - k) entries at steps of 2^k (the attention's exp table and pq_out_act8's: k = PQ_ACT8_K)
#define PQ_ACT8_K 4
static inline int16_t pq_tab_eval_k(const uint32_t *tab, int32_t v, int k)
{
    const uint32_t e = tab[(uint32_t)(v + 32768) >> k];
    const int32_t h = (int16_t)(e & 0xffff) + (((int32_t)(int16_t)(e >> 16) * (v & ((1 << k) - 1))) >> k);
    return (int16_t)(h > 32767 ? 32767 : h < -32768 ? -32768 : h);
}
static inline int16_t pq_tab_eval(const uint32_t *tab, int32_t v)
{
    const uint32_t e = tab[(uint32_t)(v + 32768) >> 6];
    const int32_t h = (int16_t)(e & 0xffff) + (((int32_t)(int16_t)(e >> 16) * (v & 63)) >> 6);
    return (int16_t)(h > 32767 ? 32767 : h < -32768 ? -32768 : h);
}

// ---- decode (pie/decode.S, c/decode.c): weight codes to int8 levels through a table
// 16 code bytes -> 64 int8 levels (2-bit: lut[b] holds the levels of the 4 codes of byte b, low byte first)
void pq_decode2(const uint8_t *codes, int nbytes, const uint32_t *lut, int8_t *out);
// 16 code bytes -> 32 int8 levels (4-bit: lut[b] = level(b & 15) | level(b >> 4) << 8)
void pq_decode4(const uint8_t *codes, int nbytes, const uint32_t *lut, int8_t *out);
// pq_decode2 with the gathers interleaved over the destination registers (PIE only)
void pq_decode2_il(const uint8_t *codes, int nbytes, const uint32_t *lut, int8_t *out);

// ---- tile (pie/tile.S, c/tile.c)
// acc[t*16 + i] = sum_k w[k*16 + i] * x[t*xstride + k], k < G (a multiple of 16), t < T
void pq_tile16(const int8_t *w, const int8_t *x, int xstride, int T, int G, int32_t *acc);
// the integer product's tile: for t < T, i < 16:
//   acc = 32 + sum_k w[k*16 + i] x[t*xstride + k] (k < G, a multiple of 16)
//   y32[t*16 + i] = sat32(y32[t*16 + i] + (((acc >> 6) * n16[i]) >> sar[t]))
void pq_tile16_n(const int8_t *w, const int8_t *x, int xstride, int T, int G, const int16_t *n16, const uint8_t *sar,
                 int32_t *y32);

// ---- hadamard (pie/hadamard.S, c/hadamard.c)
// in-place Hadamard (Sylvester order, unnormalized) of r (G = 16..128 int32) / of x (G int16) into r; returns a value
// with the bit length of max |r|
uint32_t pq_fwht_i32(int32_t *r, int G);
uint32_t pq_fwht_i16(const int16_t *x, int32_t *r, int G);
// (C only) the stages 8, 16, ..., n/2 after the first three per block of 8; returns the OR of |v|
uint32_t pq_fwht_i32_from8(int32_t *v, int n);

// ---- output (pie/output.S, c/output.c)
// h[t*ldh + i] = pq_tab_eval(tab, sat16(y32[t*16 + i] >> 8)), t < T, i < 16 (tab NULL: the identity, h = v)
void pq_out_tab16(const int32_t *y32, int T, const uint32_t *tab, int16_t *h, int ldh);
// the residual add, x (scale s) -> x (scale 2^j s, j <= n - 1, n - j <= 14), y32 at step s 2^j / 2^n:
//   x[t*ldx + i] = sat16(sat32(x[t*ldx + i] * 2^(n-j) + y32[t*16 + i]) >> n), t < T, i < 16
// (j = 0: x + (y32 >> n)); x 16-byte aligned, ldx a multiple of 8
void pq_out_add16(const int32_t *y32, int T, int n, int j, int16_t *x, int ldx);
// an int32 tile [T][16] (16 output channels) -> activation -> int8 per channel; per lane i:
//   a = sat16(acc >> r);  v = sat16(sat32(a M[i] + B[i]) >> sh);  h = tab ? pq_tab_eval_k(tab, v, PQ_ACT8_K) : v
//   q = sat(round_half_away(h N[i] / 2^sh2), -127, 127)      (vmul.s16 at CFG[7:4] = 4, then vsat)
// y[t*ldy + i] for one tile; M, B, N: the tile's 16 entries (16-byte aligned); y 16-byte aligned, ldy multiple of 16
void pq_out_act8(const int32_t *acc, int T, const int16_t *M, const int32_t *B, const int16_t *N, int r, int sh,
                 int sh2, const uint32_t *tab, int8_t *y, int ldy);
// BiasNorm / bypass on an int16 row of C values (C a multiple of 8; all pointers 16-byte aligned):
//   pq_sumsq16: sum_c (sat16(x[c] - b[c])^2 >> 8)            (the lanes' partial sums stay below 2^31 for C <= 512)
//   pq_scale_bypass16: n = sat16((x[c] * A) >> k) (floor), out[c] = sat16((xo[c] w[c] + n bb[c] + 8192) >> 14),
//   with bb = round(bypass * 16384), w = 16384 - bb (A = 1, k = 0: the plain bypass)
uint32_t pq_sumsq16(const int16_t *x, const int16_t *b, int C);
void pq_scale_bypass16(const int16_t *x, const int16_t *xo, int C, int A, int k, const int16_t *bb, const int16_t *w,
                       int16_t *out);

// ---- depthwise (pie/depthwise.S, c/depthwise.c)
// 16 channels per vector: for t < T,
//   acc[t*16 + i] = sum_k w[k*16 + i] * x[t*ostep + off[k] + i]      (int8 x int8, int32 sums; taps k < ntaps)
// x, w and every x + t*ostep + off[k] 16-byte aligned (ostep and off[] multiples of 16)
void pq_dw16(const int8_t *x, int ostep, int T, const int32_t *off, const int8_t *w, int ntaps, int32_t *acc);
// the conv module's depthwise combine (16 lanes = channels; P: Mc, Bc, Mk, Bk per lane, 16 each):
//   yc = sat16(sat32(sat16(accc >> r) Mc + Bc) >> sh);  yk = the same of acck with Mk, Bk
//   v = sat16(yc + sat16(round(yk es[t*16 + i] / 2^11)));  y[t*ldy + i] = pq_tab_eval(tab, v)
typedef struct {
    int16_t Mc[16], Mk[16];
    int32_t Bc[16], Bk[16];
} pq_comb_t;
void pq_dwcomb16(const int32_t *accc, const int32_t *acck, int T, const pq_comb_t *P, int r, int sh, const int16_t *es,
                 const uint32_t *tab, int16_t *y, int ldy);

// ---- elementwise (pie/elementwise.S, c/elementwise.c)
// q[k] = sat8(round_half_even(r[k] / 2^e)), k < n (a multiple of 16; r, q 16-byte aligned)
void pq_q8_shift(const int32_t *r, int n, int e, int8_t *q);
// GLU to int8 (16 lanes = channels): for t < T, i < 16 (rounding: nearest, ties away from 0; vmul.s16 at CFG mode 4)
//   g = sat16(round(a[t*lda + i] b[t*lda + i] / 2^15));  y[t*ldy + i] = sat(round(g N[i] / 2^sh2), -127, 127)
void pq_mul_q8(const int16_t *a, const int16_t *b, int lda, int T, const int16_t *N, int sh2, int8_t *y, int ldy);
// y[t*16 + i] = sat16(round(a[t*lda + i] b[t*lda + i] / 2^15))     (16 lanes; rounding as pq_mul_q8)
void pq_mul16(const int16_t *a, const int16_t *b, int lda, int T, int16_t *y);
// y[t*ldy + i] = sat(round(g[t*16 + i] N[i] / 2^15), -127, 127)     (16 lanes; N[i] = 2^(15 - e_i): a shift per lane)
void pq_lane_q8(const int16_t *g, int T, const int16_t *N, int8_t *y, int ldy);
// the OR of (g ^ (g >> 15)) per lane over T rows of 16 (its bit length = that of max |g| for |g| <= 32767, or one
// less for an exact negative power of 2, which int8 then holds as -128 -> sat -127): m[i], 16 lanes
void pq_lane_or16(const int16_t *g, int T, int32_t *m);
