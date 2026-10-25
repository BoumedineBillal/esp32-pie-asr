// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// Fast float functions for the model's activations (libm's cost 150-250 cycles each on the chip).
// Plain float arithmetic: with -ffp-contract=off the host and the chip compute the same bits.
// Accuracy: exp about 2e-7 relative, softplus / tanh / sigmoid a few 1e-7.
#pragma once
#include <stdint.h>
#include <string.h>

// exp(x): x = n ln2 + r, |r| <= ln2/2; exp(r) by a degree-6 Taylor polynomial; 2^n from the exponent bits.
static inline float pq_fm_exp(float x)
{
    if (x < -87.0f) {
        return 0.0f;
    }
    if (x > 88.0f) {
        x = 88.0f;
    }
    const float ln2_hi = 0.693145752f, ln2_lo = 1.42860677e-6f;
    float fn = x * 1.44269504f;
    int n = (int)(fn + (fn >= 0.0f ? 0.5f : -0.5f));
    float r = (x - (float)n * ln2_hi) - (float)n * ln2_lo;
    float p = 1.0f + r * (1.0f + r * (0.5f + r * (0.166666672f +
                                                  r * (0.0416666679f + r * (0.00833333377f + r * 0.00138888892f)))));
    uint32_t b = (uint32_t)(n + 127) << 23;
    float s;
    memcpy(&s, &b, 4);
    return p * s;
}

// log(1 + e) for e in [0, 1], without a division: e * P(e), P of degree 8 fitted on Chebyshev nodes (max relative
// error 1.9e-7 in float32)
static inline float pq_fm_log1p01(float e)
{
    float p = 0.00525345793f;
    p = p * e - 0.0295885075f;
    p = p * e + 0.0783616677f;
    p = p * e - 0.136747703f;
    p = p * e + 0.191114306f;
    p = p * e - 0.248443693f;
    p = p * e + 0.333192706f;
    p = p * e - 0.499995023f;
    p = p * e + 1.0f;
    return e * p;
}

// softplus as torch (threshold 20): z > 20 ? z : log(1 + exp(z)) = max(z, 0) + log1p(exp(-|z|))
static inline float pq_fm_softplus(float z)
{
    if (z > 20.0f) {
        return z;
    }
    float a = z < 0.0f ? -z : z;
    return (z > 0.0f ? z : 0.0f) + pq_fm_log1p01(pq_fm_exp(-a));
}

static inline float pq_fm_sigmoid(float x)
{
    return 1.0f / (1.0f + pq_fm_exp(-x));
}

// tanh(x) = sign(x) (1 - 2 / (exp(2|x|) + 1))
static inline float pq_fm_tanh(float x)
{
    float a = x < 0.0f ? -x : x;
    float t = 1.0f - 2.0f / (pq_fm_exp(2.0f * a) + 1.0f);
    return x < 0.0f ? -t : t;
}

// ---- table versions, branch-free (about 12 instructions, no tests: the compiler interleaves independent calls).
// Linear interpolation on a grid of step 1/32 over [0, 16] of one-sided functions, the sign handled by fabs /
// copysign / fmax:
//   softplus(z) = max(z, 0) + G(min(|z|, 16)),  G(a) = log1p(exp(-a))      (G(16) = 1.1e-7)
//   tanh(x)     = copysign(T(min(|x|, 16)), x)
//   sigmoid(x)  = 0.5 + copysign(S(min(|x|, 16)), x),  S(a) = sigmoid(a) - 0.5
//   exp(x)      = E(min(-x, 16)) for x <= 0,  E(a) = exp(-a)               (exp(-16) = 1.1e-7)
// Interpolation error <= max|f''| h^2 / 8: about 3e-5 (G, S, E) and 8e-5 (T) absolute.
#define PQ_FM_TAB_HI 16.0f
#define PQ_FM_TAB_STEPS 32.0f
#define PQ_FM_TAB_N 512
extern float *pq_fm_tab_g, *pq_fm_tab_s, *pq_fm_tab_t, *pq_fm_tab_e;   // [PQ_FM_TAB_N + 2] each
#include <stddef.h>
int pq_fm_tables_init(void *(*alloc)(size_t));   // the tables (8 KB) from alloc, once

static inline float pq_fm_min(float a, float b)
{
#if defined(__riscv)
    float r;
    __asm__("fmin.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b));
    return r;
#else
    return __builtin_fminf(a, b);
#endif
}

static inline float pq_fm_max(float a, float b)
{
#if defined(__riscv)
    float r;
    __asm__("fmax.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b));
    return r;
#else
    return __builtin_fmaxf(a, b);
#endif
}

// T at a in [0, 16]: the grid index (truncation of a non-negative value), then the interpolation
static inline float pq_fm_tab1(const float *T, float a)
{
    const float p = pq_fm_min(a, PQ_FM_TAB_HI) * PQ_FM_TAB_STEPS;
    const int i = (int)p;
    const float f = p - (float)i;
    return T[i] + f * (T[i + 1] - T[i]);
}

static inline float pq_fm_softplus_tab(float z)
{
    return pq_fm_max(z, 0.0f) + pq_fm_tab1(pq_fm_tab_g, __builtin_fabsf(z));
}

// pq_fm_swoosh_l_tab(x) = softplus(x - 4) - 0.08 x - 0.035
static inline float pq_fm_swoosh_l_tab(float x)
{
    return pq_fm_softplus_tab(x - 4.0f) - 0.08f * x - 0.035f;
}

// pq_fm_swoosh_r_tab(x) = softplus(x - 1) - 0.08 x - 0.313261687
static inline float pq_fm_swoosh_r_tab(float x)
{
    return pq_fm_softplus_tab(x - 1.0f) - 0.08f * x - 0.313261687f;
}

static inline float pq_fm_sigmoid_tab(float x)
{
    return 0.5f + __builtin_copysignf(pq_fm_tab1(pq_fm_tab_s, __builtin_fabsf(x)), x);
}

static inline float pq_fm_tanh_tab(float x)
{
    return __builtin_copysignf(pq_fm_tab1(pq_fm_tab_t, __builtin_fabsf(x)), x);
}

// exp(x) for x <= 0 (softmax after subtracting the max)
static inline float pq_fm_exp_neg_tab(float x)
{
    return pq_fm_tab1(pq_fm_tab_e, -x);
}
