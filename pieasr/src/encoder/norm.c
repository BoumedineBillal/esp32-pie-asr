// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// BiasNorm and the bypasses, on float rows and on the int16 residual.
#include <math.h>
#include "encoder/encoder_internal.h"

// x [T, C] in place: x * exp(log_scale) / sqrt(mean_c((x - bias)^2))
void pq_bias_norm(float *x, int T, int C, const uint16_t *bias, float scale)
{
    float bf[512];   // the bias as floats once per call (C <= 512), not once per element
    for (int c = 0; c < C; c++) {
        bf[c] = pq_h2f(bias, c);
    }
    for (int t = 0; t < T; t++) {
        float *r = x + (size_t)t * C;
        float ms = 0.0f;
        for (int c = 0; c < C; c++) {
            float d = r[c] - bf[c];
            ms += d * d;
        }
        ms /= (float)C;
        float s = scale / sqrtf(ms);
        for (int c = 0; c < C; c++) {
            r[c] *= s;
        }
    }
}

// x = x_orig + (x - x_orig) * s[c]
void pq_bypass(const float *x_orig, float *x, int T, int C, const uint16_t *s)
{
    float sf[512];   // the scales as floats once per call (C <= 512)
    for (int c = 0; c < C; c++) {
        sf[c] = pq_h2f(s, c);
    }
    for (int t = 0; t < T; t++) {
        for (int c = 0; c < C; c++) {
            size_t i = (size_t)t * C + c;
            x[i] = x_orig[i] + (x[i] - x_orig[i]) * sf[c];
        }
    }
}

// int16 residual (PIEASR_I16_RES): the bypass scales b[c] (in [0, 1]) as bb = round(b 16384) and w = 16384 - bb
static void bypass_q14(const uint16_t *s, int C, int16_t *bb, int16_t *w)
{
    for (int c = 0; c < C; c++) {
        bb[c] = (int16_t)pq_round(pq_h2f(s, c) * 16384.0f);
        w[c] = (int16_t)(16384 - bb[c]);
    }
}

// x = xo + (x - xo) b[c] = (xo w + x bb + 8192) >> 14 per row (pq_scale_bypass16 with A = 1, k = 0)
void pq_bypass16(const int16_t *xo, int16_t *x, int T, int C, const uint16_t *s)
{
    int16_t bb[512] __attribute__((aligned(16))), w[512] __attribute__((aligned(16)));
    bypass_q14(s, C, bb, w);
    for (int t = 0; t < T; t++) {
        pq_scale_bypass16(x + (size_t)t * C, xo + (size_t)t * C, C, 1, 0, bb, w, x + (size_t)t * C);
    }
}

// int16 residual: x8 (scale s 2^j) -> BiasNorm -> bypass with xo (scale s) -> x (scale s). Per frame: the sum of
// squares (pq_sumsq16, squares >> 8), the factor F = norm scale / (rms s) (x8's scale cancels) as A 2^-k
// (A < 2^15), then pq_scale_bypass16: n = (x8 A) >> k, x = (xo w + n bb + 8192) >> 14
void pq_norm_bypass16(int16_t *x, const int16_t *xo, int T, int C, const uint16_t *bias, float scale,
                      const uint16_t *byp, float s, int j)
{
    int16_t b16[512] __attribute__((aligned(16))), bb[512] __attribute__((aligned(16)));
    int16_t w[512] __attribute__((aligned(16)));
    const float s8 = s * (float)(1 << j);
    for (int c = 0; c < C; c++) {
        const int v = pq_round(pq_h2f(bias, c) / s8);
        b16[c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    bypass_q14(byp, C, bb, w);
    for (int t = 0; t < T; t++) {
        int16_t *r = x + (size_t)t * C;
        const uint32_t ss = pq_sumsq16(r, b16, C);
        const float rms = sqrtf((float)ss * 256.0f / (float)C);
        const float F = rms > 0.0f ? scale / (rms * s) : 0.0f;
        int k = 0;
        while (k < 30 && F * (float)(1 << (k + 1)) < 32767.0f) {
            k++;
        }
        int A = pq_round(F * (float)(1 << k));
        A = A > 32767 ? 32767 : A;
        pq_scale_bypass16(r, xo + (size_t)t * C, C, A, k, bb, w, r);
    }
}
