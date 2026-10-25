// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The recognizer: a streaming Zipformer2 transducer (encoder, decoder, joiner) on 2/4-bit weights.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIEASR_MEL 80          // feature bins per frame
#define PIEASR_CHUNK_IN 141    // feature frames given to one pieasr_encode_chunk (128 new + 13 margin)
#define PIEASR_CHUNK_STEP 128  // feature frames consumed per chunk (1.28 s)
#define PIEASR_OUT_T 32        // encoder output frames per chunk (25 Hz)
#define PIEASR_DIM 512         // encoder output, decoder and joiner width
#define PIEASR_VOCAB 650       // tokens (0 = blank)

typedef struct pieasr_model pieasr_model_t;

// ---- options (set before pieasr_init)
extern size_t pieasr_opt_ws_floats;   // the workspace, in floats
// 2: the int8 products in the PIE organization (the chip's path); 1: the plain C int8 products; 0: float
extern int pieasr_opt_q8;
extern int pieasr_opt_int;            // the 2/4-bit products in integer arithmetic
// 1: pieasr_init writes the int8 copies of weights over their sources in the weights buffer (then only
// pieasr_opt_q8 == 2 works; the buffer must be writable)
extern int pieasr_opt_inplace;
extern int pieasr_opt_i16;            // int16 activations (PIEASR_I16_*) with pieasr_opt_int
#define PIEASR_I16_FF 1               // the feed-forward hidden (may change after init)
#define PIEASR_I16_RES 4              // the residual stream of each layer
#define PIEASR_I16_CV 16              // the conv modules
#define PIEASR_I16_NA 32              // NonlinAttention's values and gate
#define PIEASR_I16_SA 64              // the self-attentions' values
#define PIEASR_I16_AW 128             // the attention weights: q, k, position term, softmax
// the stable configuration (all of the above)
#define PIEASR_I16_ALL (PIEASR_I16_FF | PIEASR_I16_RES | PIEASR_I16_CV | PIEASR_I16_NA | PIEASR_I16_SA | PIEASR_I16_AW)
extern int pieasr_greedy_win;         // frames per joiner product in pieasr_greedy_chunk (any value: the same tokens)

// ---- timing: per part of the encoder and the greedy search, summed in units of pieasr_clock (if set)
typedef struct {
    uint64_t front_conv, front_convnext, linear, attn, conv_dw, total;
    uint64_t m_attnw, m_ff, m_na, m_sa, m_conv, m_norm, front;   // whole modules, products included
    uint64_t cnx_dw, cnx_pw1, cnx_pw2, cnx_scatter, sc_qk, sc_pos, av;   // pieces
    uint64_t g_dec, g_join, g_tokens;   // greedy: decoder updates, joiner products, tokens emitted
    uint64_t layers, ffsw;              // all encoder layers; the feed-forward SwooshL maps
    uint64_t dw_glu, dw_q, dw_k, dw_comb, dw_out;   // conv-module depthwise pieces (core 0, cycles)
} pieasr_prof_t;
extern pieasr_prof_t pieasr_prof;
extern uint64_t (*pieasr_clock)(void);

// optional trace of named intermediate tensors (host checks)
typedef void (*pieasr_trace_fn)(const char *name, const float *data, int n);
extern pieasr_trace_fn pieasr_trace;

// ---- the model
size_t pieasr_model_size(void);   // the bytes of a pieasr_model_t
// weights: the weights file in memory (16-byte aligned, kept for the model's life); alloc(bytes) for the state, the
// tables and the workspace (PSRAM on the chip). Also starts the two-core runtime and the PIE tables. 0 = ok.
int pieasr_init(pieasr_model_t *m, const void *weights, size_t size, void *(*alloc)(size_t));
void pieasr_reset(pieasr_model_t *m);   // a new stream: zero states, the decoder's context blank
// x [PIEASR_CHUNK_IN][PIEASR_MEL] features -> enc [PIEASR_OUT_T][PIEASR_DIM]
void pieasr_encode_chunk(pieasr_model_t *m, const float *x, float *enc);
// one greedy step on an encoder frame; returns the token (0 = blank)
int pieasr_greedy_step(pieasr_model_t *m, const float *enc_frame);
// greedy search over the T frames of a chunk: tok[t] (0 = blank), the same tokens as pieasr_greedy_step per frame
void pieasr_greedy_chunk(pieasr_model_t *m, const float *enc, int T, int *tok);
// the vocabulary: PIEASR_VOCAB + 2 pieces (U+2581 starts a word), NULL where the weights file has none
const char *const *pieasr_tokens(const pieasr_model_t *m);
size_t pieasr_workspace_peak(const pieasr_model_t *m);   // the most workspace used so far, in floats

#ifdef __cplusplus
}
#endif
