// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The features: 80-bin log-mel frames, Kaldi-compatible, from 16 kHz audio.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIEASR_FBANK_SHIFT 160   // the frame shift in samples (10 ms; the window is 400 samples)

typedef struct pieasr_fbank pieasr_fbank_t;

size_t pieasr_fbank_size(void);   // the bytes of a pieasr_fbank_t
void pieasr_fbank_init(pieasr_fbank_t *fb);
// frames of a finished signal of n samples (Kaldi's NumFrames with snip_edges false)
int pieasr_fbank_num_frames(int n);
// out [frames][PIEASR_MEL] from float audio in [-1, 1]
void pieasr_fbank_compute(const pieasr_fbank_t *fb, const float *audio, int n, float *out);
// the same from int16 samples (sample / 32768: the same values as a float signal read from a 16-bit file)
void pieasr_fbank_compute_pcm16(const pieasr_fbank_t *fb, const int16_t *pcm, int n, float *out);

#ifdef __cplusplus
}
#endif
