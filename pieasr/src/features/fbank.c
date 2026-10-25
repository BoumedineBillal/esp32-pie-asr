// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// 80-bin log-mel features, Kaldi-compatible: 16 kHz, 25 ms window (400), 10 ms shift (160), snip_edges false
// (frame i starts at 160 i - 120, samples outside the signal reflected), no dither, DC removed, pre-emphasis 0.97,
// Povey window, 512-point FFT, power spectrum, mel bins over 20-7600 Hz, log(max(e, FLT_EPSILON)). The audio is float
// in [-1, 1] (not scaled to int16).
#include <float.h>
#include <math.h>
#include <string.h>
#include "math/detmath.h"
#include "pieasr/features.h"
#include "runtime/parallel.h"

#define BINS 80

struct pieasr_fbank {
    float window[400];
    // mel filters: for each bin, the first FFT index and its weights
    int16_t first[BINS], count[BINS];
    float weights[BINS][64];
};

size_t pieasr_fbank_size(void)
{
    return sizeof(pieasr_fbank_t);
}

// All math is detmath.h's (IEEE +, -, *, / only), so the host computes the chip's features bit for bit; libm's log,
// cos, sin, pow (tables) and logf (per frame) differ in the last bit between glibc and newlib.

#define WIN 400
#define NFFT 512

static void fft_init(void);

static double mel_scale(double f)
{
    return 1127.0 * dm_log(1.0 + f / 700.0);
}

void pieasr_fbank_init(pieasr_fbank_t *fb)
{
    fft_init();
    for (int i = 0; i < WIN; i++) {
        fb->window[i] = (float)dm_pow(0.5 - 0.5 * dm_cos(2.0 * M_PI * i / (WIN - 1)), 0.85);
    }
    const double lo = mel_scale(20.0), hi = mel_scale(8000.0 - 400.0);
    const double delta = (hi - lo) / (BINS + 1);
    for (int b = 0; b < BINS; b++) {
        double left = lo + b * delta, center = left + delta, right = center + delta;
        fb->first[b] = -1;
        fb->count[b] = 0;
        for (int i = 0; i < NFFT / 2; i++) {
            double m = mel_scale(16000.0 / NFFT * i);
            if (m > left && m < right) {
                double w = m <= center ? (m - left) / (center - left) : (right - m) / (right - center);
                if (fb->first[b] < 0) {
                    fb->first[b] = (int16_t)i;
                }
                fb->weights[b][fb->count[b]++] = (float)w;
            }
        }
    }
}

int pieasr_fbank_num_frames(int n)
{
    return (n + PIEASR_FBANK_SHIFT / 2) / PIEASR_FBANK_SHIFT;
}

// twiddles of every stage: tw[len / 2 + k] = exp(-2 pi i k / len), k < len / 2; built by pieasr_fbank_init
static float tw_re[NFFT], tw_im[NFFT];

static void fft_init(void)
{
    for (int len = 2; len <= NFFT; len <<= 1) {
        double a = -2.0 * M_PI / len;
        for (int k = 0; k < len / 2; k++) {
            tw_re[len / 2 + k] = (float)dm_cos(a * k);
            tw_im[len / 2 + k] = (float)dm_sin(a * k);
        }
    }
}

// in-place radix-2 complex FFT of N points (N <= NFFT / 2 here: the real FFT's half-size transform)
static void fft(float *re, float *im, int N)
{
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }
    for (int len = 2; len <= N; len <<= 1) {
        for (int k = 0; k < len / 2; k++) {
            float wr = tw_re[len / 2 + k], wi = tw_im[len / 2 + k];
            for (int i = k; i < N; i += len) {
                int j = i + len / 2;
                float xr = re[j] * wr - im[j] * wi, xi = re[j] * wi + im[j] * wr;
                re[j] = re[i] - xr;
                im[j] = im[i] - xi;
                re[i] += xr;
                im[i] += xi;
            }
        }
    }
}

static void compute(const pieasr_fbank_t *fb, const float *audio, const int16_t *pcm, int n, float *out);

void pieasr_fbank_compute(const pieasr_fbank_t *fb, const float *audio, int n, float *out)
{
    compute(fb, audio, NULL, n, out);
}

void pieasr_fbank_compute_pcm16(const pieasr_fbank_t *fb, const int16_t *pcm, int n, float *out)
{
    compute(fb, NULL, pcm, n, out);
}

// one frame: Kaldi's window processing, then the power spectrum of the 512-point real FFT computed as a 256-point
// complex FFT of z[m] = w[2m] + i w[2m+1] and the split X[k] = E[k] + exp(-2 pi i k / 512) O[k], with
// E[k] = (Z[k] + conj Z[256-k]) / 2 and O[k] = (Z[k] - conj Z[256-k]) / 2i
static void frame(const pieasr_fbank_t *fb, const float *audio, const int16_t *pcm, int n, int f, float *out)
{
    const int H = NFFT / 2;
    float w[NFFT], zr[NFFT / 2], zi[NFFT / 2], pw[NFFT / 2 + 1];
    const int start = f * PIEASR_FBANK_SHIFT + PIEASR_FBANK_SHIFT / 2 - WIN / 2;
    float mean = 0.0f;
    for (int i = 0; i < WIN; i++) {
        int s = start + i;
        while (s < 0 || s >= n) {   // reflect at both ends (Kaldi ExtractWindow)
            s = s < 0 ? -s - 1 : 2 * n - 1 - s;
        }
        w[i] = audio ? audio[s] : pcm[s] / 32768.0f;
        mean += w[i];
    }
    mean /= WIN;
    for (int i = 0; i < WIN; i++) {
        w[i] -= mean;
    }
    for (int i = WIN - 1; i > 0; i--) {
        w[i] -= 0.97f * w[i - 1];
    }
    w[0] -= 0.97f * w[0];
    for (int i = 0; i < WIN; i++) {
        w[i] *= fb->window[i];
    }
    memset(w + WIN, 0, sizeof(float) * (NFFT - WIN));
    for (int m = 0; m < H; m++) {
        zr[m] = w[2 * m];
        zi[m] = w[2 * m + 1];
    }
    fft(zr, zi, H);
    const float x0 = zr[0] + zi[0], xh = zr[0] - zi[0];
    pw[0] = x0 * x0;
    pw[H] = xh * xh;
    for (int k = 1; k < H; k++) {
        const float A = zr[k], B = zi[k], C = zr[H - k], D = zi[H - k];
        const float er = 0.5f * (A + C), ei = 0.5f * (B - D), or_ = 0.5f * (B + D), oi = -0.5f * (A - C);
        const float wr = tw_re[H + k], wi = tw_im[H + k];
        const float xr = er + (wr * or_ - wi * oi), xi = ei + (wr * oi + wi * or_);
        pw[k] = xr * xr + xi * xi;
    }
    for (int b = 0; b < BINS; b++) {
        float e = 0;
        for (int k = 0; k < fb->count[b]; k++) {
            e += fb->weights[b][k] * pw[fb->first[b] + k];
        }
        out[b] = dm_logf(e > FLT_EPSILON ? e : FLT_EPSILON);
    }
}

typedef struct {
    const pieasr_fbank_t *fb;
    const float *audio;
    const int16_t *pcm;
    int n, frames;
    float *out;
} fb_job_t;

static void fb_part(void *ctx, int part, int parts)
{
    const fb_job_t *j = ctx;
    int f0, f1;
    pq_par_range(j->frames, part, parts, 1, &f0, &f1);
    for (int f = f0; f < f1; f++) {
        frame(j->fb, j->audio, j->pcm, j->n, f, j->out + (size_t)f * BINS);
    }
}

// frames split between the two cores
static void compute(const pieasr_fbank_t *fb, const float *audio, const int16_t *pcm, int n, float *out)
{
    fb_job_t j = { .fb = fb, .audio = audio, .pcm = pcm, .n = n, .frames = pieasr_fbank_num_frames(n), .out = out };
    pq_par_run(fb_part, &j);
}
