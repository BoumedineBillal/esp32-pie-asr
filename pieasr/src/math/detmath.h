// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// Deterministic math: the same bits on the host (x86-64, SSE) and on the chip (soft double, hard float), because
// every function is built from IEEE +, -, *, / only (and exact bit operations), compiled with -ffp-contract=off on
// both. glibc's and newlib's exp / log / tanh / cos / sin / pow differ in the last bit for some inputs; with them the
// host's features and int16 activation tables, and so the encoder's output, would differ from the chip's.
//
// dm_*: double precision (about 1e-16 relative), for the tables built once at init (cast to float afterwards).
// dm_logf: float precision (about 1 ulp), for the per-frame log-mel features (soft double would be too slow there).
#pragma once
#include <stdint.h>
#include <string.h>

static inline double dm_bits_to_d(uint64_t b)
{
    double d;
    memcpy(&d, &b, 8);
    return d;
}

static inline uint64_t dm_d_to_bits(double d)
{
    uint64_t b;
    memcpy(&b, &d, 8);
    return b;
}

// 2^k for -1022 <= k <= 1023, exact
static inline double dm_pow2i(int k)
{
    return dm_bits_to_d((uint64_t)(k + 1023) << 52);
}

// Series coefficients as compile-time constants (the compiler rounds each one correctly, the same on both machines),
// so the loops multiply instead of dividing: a soft-double division costs the P4 several times a multiplication.
static const double dm_inv_fact[17] = { 1.0, 1.0, 1.0 / 2, 1.0 / 6, 1.0 / 24, 1.0 / 120, 1.0 / 720, 1.0 / 5040,
                                        1.0 / 40320, 1.0 / 362880, 1.0 / 3628800, 1.0 / 39916800, 1.0 / 479001600,
                                        1.0 / 6227020800.0, 1.0 / 87178291200.0, 1.0 / 1307674368000.0,
                                        1.0 / 20922789888000.0 };
// 1 / ((i - 1) i) for i = 0..25 (entries 0 and 1 unused)
static const double dm_inv_pair[26] = { 0, 0, 1.0 / 2, 1.0 / 6, 1.0 / 12, 1.0 / 20, 1.0 / 30, 1.0 / 42, 1.0 / 56,
                                        1.0 / 72, 1.0 / 90, 1.0 / 110, 1.0 / 132, 1.0 / 156, 1.0 / 182, 1.0 / 210,
                                        1.0 / 240, 1.0 / 272, 1.0 / 306, 1.0 / 342, 1.0 / 380, 1.0 / 420, 1.0 / 462,
                                        1.0 / 506, 1.0 / 552, 1.0 / 600 };
// 1 / i for odd i = 1..41 (index (i - 1) / 2)
static const double dm_inv_odd[21] = { 1.0, 1.0 / 3, 1.0 / 5, 1.0 / 7, 1.0 / 9, 1.0 / 11, 1.0 / 13, 1.0 / 15, 1.0 / 17,
                                       1.0 / 19, 1.0 / 21, 1.0 / 23, 1.0 / 25, 1.0 / 27, 1.0 / 29, 1.0 / 31, 1.0 / 33,
                                       1.0 / 35, 1.0 / 37, 1.0 / 39, 1.0 / 41 };

#define DM_LN2_HI 6.93147180369123816490e-01   /* fdlibm's split of ln 2: hi has 21 trailing zero bits */
#define DM_LN2_LO 1.90821492927058770002e-10
#define DM_PI 3.14159265358979311600e+00

// exp(x): x = k ln2 + r, |r| <= ln2/2, exp(r) by its Taylor series to r^16 (|r|^17/17! < 3e-19)
static inline double dm_exp(double x)
{
    if (x < -700.0) {
        return 0.0;
    }
    if (x > 700.0) {
        x = 700.0;
    }
    double fk = x * 1.44269504088896338700e+00;
    int k = (int)(fk + (fk >= 0.0 ? 0.5 : -0.5));
    double r = (x - (double)k * DM_LN2_HI) - (double)k * DM_LN2_LO;
    double p = dm_inv_fact[16];
    for (int i = 15; i >= 0; i--) {
        p = p * r + dm_inv_fact[i];
    }
    // k may reach about +-1010: split the scaling so 2^k stays a normal number
    if (k < -1000) {
        return p * dm_pow2i(k + 500) * dm_pow2i(-500);
    }
    return p * dm_pow2i(k);
}

// log(x), x > 0 and normal: x = m 2^e with m in [sqrt(1/2), sqrt(2)); log m = 2 atanh(s), s = (m - 1) / (m + 1),
// |s| <= 0.1716: the series to s^31 (error below 1e-24)
static inline double dm_log(double x)
{
    if (!(x > 0.0)) {
        return -1.0e300;
    }
    uint64_t b = dm_d_to_bits(x);
    int e = (int)((b >> 52) & 0x7ff) - 1023;
    double m = dm_bits_to_d((b & 0x000fffffffffffffull) | 0x3ff0000000000000ull);
    if (m > 1.41421356237309514547) {
        m = m * 0.5;
        e = e + 1;
    }
    double s = (m - 1.0) / (m + 1.0), s2 = s * s, sum = 0.0;
    for (int i = 31; i >= 1; i -= 2) {
        sum = sum * s2 + dm_inv_odd[(i - 1) / 2];
    }
    return (double)e * DM_LN2_HI + ((double)e * DM_LN2_LO + 2.0 * s * sum);
}

// log(1 + y), y > -1: the series for small y, else log(u) corrected for the rounding of u = 1 + y
static inline double dm_log1p(double y)
{
    if (y < 1e-4 && y > -1e-4) {
        return y * (1.0 - y * (0.5 - y * (1.0 / 3.0 - y * (0.25 - y * 0.2))));
    }
    double u = 1.0 + y;
    return dm_log(u) - ((u - 1.0) - y) / u;
}

// tanh(x) = sign(x) (1 - 2 / (exp(2|x|) + 1)); the series for small |x|
static inline double dm_tanh(double x)
{
    double a = x < 0.0 ? -x : x;
    double t;
    if (a < 1e-3) {
        double a2 = a * a;
        t = a * (1.0 - a2 * (1.0 / 3.0 - a2 * (2.0 / 15.0)));
    } else if (a > 20.0) {
        t = 1.0;
    } else {
        t = 1.0 - 2.0 / (dm_exp(2.0 * a) + 1.0);
    }
    return x < 0.0 ? -t : t;
}

// sin and cos for |x| <= about 1e3: x = n pi/2 + r, |r| <= pi/4 (pi/2 split in three parts, as fdlibm),
// then the Taylor series of sin r and cos r to r^25 / r^24
static inline void dm_sincos(double x, double *sn, double *cs)
{
    const double pio2_1 = 1.57079632673412561417e+00, pio2_2 = 6.07710050650619224932e-11,
                 pio2_3 = 2.02226624879595063154e-21;
    double fn = x * 6.36619772367581382433e-01;
    int n = (int)(fn + (fn >= 0.0 ? 0.5 : -0.5));
    double r = ((x - (double)n * pio2_1) - (double)n * pio2_2) - (double)n * pio2_3;
    // sin r = r (1 - r^2/(2*3) (1 - r^2/(4*5) (1 - ...))), cos r = 1 - r^2/(1*2) (1 - r^2/(3*4) (1 - ...))
    double r2 = r * r, s = 1.0, c = 1.0;
    // |r| <= pi/4: the first omitted terms, r^21/21! and r^20/20!, are below 1e-20
    for (int i = 19; i >= 3; i -= 2) {
        s = 1.0 - r2 * dm_inv_pair[i] * s;
    }
    s = r * s;
    for (int i = 18; i >= 2; i -= 2) {
        c = 1.0 - r2 * dm_inv_pair[i] * c;
    }
    switch (n & 3) {
    case 0: *sn = s; *cs = c; break;
    case 1: *sn = c; *cs = -s; break;
    case 2: *sn = -s; *cs = -c; break;
    default: *sn = -c; *cs = s; break;
    }
}

static inline double dm_sin(double x)
{
    double s, c;
    dm_sincos(x, &s, &c);
    return s;
}

static inline double dm_cos(double x)
{
    double s, c;
    dm_sincos(x, &s, &c);
    return c;
}

// atan(x): |x| > 1 by pi/2 - atan(1/|x|); above tan(pi/12) by pi/6 + atan((a sqrt3 - 1) / (a + sqrt3)); then the
// series for |a| <= 0.268 to a^41 (error below 1e-23)
static inline double dm_atan(double x)
{
    double a = x < 0.0 ? -x : x, off = 0.0;
    int inv = a > 1.0;
    if (inv) {
        a = 1.0 / a;
    }
    if (a > 0.26794919243112270) {
        a = (a * 1.73205080756887719 - 1.0) / (a + 1.73205080756887719);
        off = DM_PI / 6.0;
    }
    double a2 = a * a, q = 0.0;
    for (int k = 20; k >= 0; k--) {
        q = q * -a2 + dm_inv_odd[k];
    }
    double r = off + a * q;
    if (inv) {
        r = DM_PI / 2.0 - r;
    }
    return x < 0.0 ? -r : r;
}

// a^b for a >= 0
static inline double dm_pow(double a, double b)
{
    return a > 0.0 ? dm_exp(b * dm_log(a)) : 0.0;
}

// log(x) in float for x > 0 normal: x = m 2^e, m in [sqrt(1/2), sqrt(2)), s = (m - 1) / (m + 1),
// log m = 2 (s + s^3/3 + ... + s^9/9) (|s| <= 0.1716: the next term is below 2e-9 of the value)
static inline float dm_logf(float x)
{
    uint32_t b;
    memcpy(&b, &x, 4);
    int e = (int)((b >> 23) & 0xff) - 127;
    uint32_t mb = (b & 0x007fffffu) | 0x3f800000u;
    float m;
    memcpy(&m, &mb, 4);
    if (m > 1.41421354f) {
        m = m * 0.5f;
        e = e + 1;
    }
    const float s = (m - 1.0f) / (m + 1.0f), s2 = s * s;
    const float p = s2 * (0.333333343f + s2 * (0.200000003f + s2 * (0.142857149f + s2 * 0.111111112f)));
    const float lm = 2.0f * s + 2.0f * s * p;
    return (float)e * 0.693145752f + ((float)e * 1.42860677e-6f + lm);
}
