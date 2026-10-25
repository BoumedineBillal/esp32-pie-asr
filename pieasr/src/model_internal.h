// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The model's weights and state (struct pieasr_model), and what the engine's parts share.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "kernels/dense_int8.h"
#include "kernels/linear.h"
#include "pieasr/model.h"
#include "weights/weights_file.h"

#define PQ_EMBED_T 64        // frames after the front end (50 Hz)
#define PQ_LEFT 256          // attention left context at 50 Hz
#define PQ_STACKS 6
#define PQ_MAX_LAYERS 4

#define QD 32        // query / key head dim
#define PD 4         // position head dim
#define VD 12        // value head dim
#define POS_DIM 48

// one 2/4-bit matrix product
typedef struct {
    pq_cqmat_t w;
    const uint16_t *b;       // float16 bias or NULL
    float ax, ay;            // calibrated max |input|, max |output| (math/tables pq_scales); 0: none
    pqi_t qi;                // the integer product (pieasr_opt_int), prepared at init
} pq_lin_t;

typedef struct {
    pq_lin_t aw_in, sa_in[2], sa_out[2], ff_in[3], ff_out[3], na_in, na_out, cv_in[2], cv_out[2];
    const uint16_t *cv_causal_w[2], *cv_causal_b[2], *cv_chunk_w[2], *cv_chunk_b[2], *cv_scale[2];
    const uint16_t *norm_bias, *bypass_mid, *bypass;
    float norm_scale;        // exp(norm.log_scale)
    float *pos;              // linear_pos(rel_pos_table(L, S)) [L+2S-1, 4H], computed at init
    // the int8 path: depthwise taps in 16-channel blocks [D/16][taps][16], scale per channel; float biases
    int8_t *cv_wc8[2], *cv_wk8[2];
    float *cv_swc[2], *cv_swk[2], *cv_bcf[2], *cv_bkf[2];
    float *cv_es[2];         // the chunk edge scale 1 + (left + right) as floats [D][S] (precomputed)
    // streaming state (inside the state buffer)
    float *key, *nonlin, *val[2], *conv[2];
    // pieasr_opt_int: the attention caches in int8 instead (key, nonlin, val stay NULL): keys with a scale per key and
    // head (ks [L][H], as the int8 q.k product quantizes them), values with a static scale per channel (calibrated)
    int8_t *key8, *nl8, *val8[2];
    float *ks;
    float *nl_s, *nl_is, *val_s[2], *val_is[2];   // per-channel scale and its inverse
    // PIEASR_I16_FF: the feed-forward hidden int16 (in_proj's output through SwooshL, out_proj's input scale)
    uint32_t *ff_tab[3];
    // PIEASR_I16_RES: the residual stream int16, one scale res_s for x0..x7, mid, out (calibrated max / 32767);
    // x8 (the last feed-forward's sum, read only by the BiasNorm) at res_s 2^res_j
    float res_s;
    int res_j;
    // PIEASR_I16_CV: the conv modules in integers. cv_in writes int16 (a: identity, b: sigmoid table at 1 / 32768);
    // the GLU int16 (pq_mul16) and the cache int16 [D/16][lp][16] (over the float cache's storage); per 16-channel
    // block and chunk a power-of-2 scale per channel from the max over [cache | chunk] -> int8 (pq_lane_or16,
    // pq_lane_q8), the two depthwise convs (pq_dw16), the combine's multipliers per block (from cv_cc), pq_dwcomb16
    // (es16: the edge scales in Q11, [D/16][S][16]) + the SwooshR table -> int16 at cv_out's input scale
    uint32_t *cv_tabb[2], *cv_tabr[2];
    int16_t *cv_es16[2];
    int cv_r[2];
    float cv_sv[2];          // the combine's output step
    float *cv_cc[2];         // per channel: [0..D) taps' causal factor, [D..2D) chunk factor (cv_in step 2^r / s_v),
                             // [2D..3D), [3D..4D) the biases / s_v
    // PIEASR_I16_NA: na_in writes int16 (the first hd outputs through a tanh table at 1 / 32768, the rest as is); the
    // values tanh(s) v -> int8 per channel (pq_mul_q8, N / 2^sh2 = na_in's step / nl_s); the gate int16
    uint32_t *na_tab;
    int16_t *na_N;
    int na_sh2;
    // PIEASR_I16_SA: sa_in writes the int8 values directly (N / 2^sh = sa_in's step / val_s per channel)
    int16_t *sa_N[2];
    int sa_sh[2];
    // PIEASR_I16_AW: the attention weights in integers. aw_in writes q, k, pq as int8 at static per-tensor scales
    // (N / 2^aw_sh per column; the calibrated aw.q, aw.k, aw.pq); the key cache int8 at that static scale (ks unused);
    // the position table int8 per head (pos8 [npos][4H], scale per head); per score
    //   v = sat16(sat32(sat16(qk >> aw_r1) Mqk + sat16(b >> 1) Mpos[h] + 2^(sh - 1)) >> aw_sh2)    (b = pq . pos)
    // at the step aw_sv (2 x the calibrated max |score| / 32767); softmax: e = aw_exp(v - row max) in 0..127
    // (4096-entry table), pq_att8 = e, pq_att8_s = 1 / sum e
    int16_t *aw_N;
    int8_t *pos8;
    uint32_t *aw_exp;
    int aw_sh, aw_r1, aw_sh2;
    int32_t aw_Mqk, aw_Mpos[8];
} pq_layer_t;

typedef struct {
    int layers, D, ds, H, ff, K, L, S;
    float ds_w[8];           // softmax(downsample.bias)
    const uint16_t *out_combiner;
    pq_layer_t layer[PQ_MAX_LAYERS];
} pq_stack_t;

struct pieasr_model {
    pq_weights_t weights;
    // front end
    const uint16_t *conv0_w, *conv0_b, *conv4_w, *conv4_b, *conv7_w, *conv7_b;
    const uint16_t *dw_w, *dw_b, *pw1_w, *pw1_b, *pw2_w, *pw2_b, *out_norm_bias;
    float out_norm_scale;
    pq_lin_t embed_out;
    pq_stack_t stack[PQ_STACKS];
    int16_t *res16;          // the int16 residual of the running layer (PIEASR_I16_RES), or NULL; products whose input
    int res_j;               // or residual-add output is this buffer (passed as a float pointer) use it as int16
    float out_ds_w[2];
    pq_lin_t enc_proj;
    // decoder / joiner
    const pq_weights_entry_t *emb, *dec_proj, *out_lin;
    const uint16_t *dec_conv_w, *dec_proj_b, *out_lin_b;
    const char *tokens[PIEASR_VOCAB + 2];
    // state
    uint8_t *state;          // all caches, one buffer
    size_t state_bytes;
    float *left_pad;         // [128, 3, 19]
    int processed;
    // the int8 path (pieasr_opt_q8 == 2): dense int8 front-end convs, decoder_proj and joiner; float copies of small
    // weights
    pq_dense_t conv4_q, conv7_q, pw1_q, pw2_q, dec_proj_q, out_lin_q;
    float *dw_wf;            // ConvNeXt depthwise 7x7 [128][49]
    int8_t *dw_w8;           // the same, int8 [8][49][16], scale dw_sw [128]; bias dw_bf [128]
    float *dw_sw, *dw_bf;
    float *out_lin_bf;       // joiner output bias [650]
    float *pw1_is;           // pieasr_opt_int: the ConvNeXt hidden's int8 scales per channel, inverse [384] (folded
                             // into pw2's weights)
    // greedy search
    int ctx[2];
    float dec[PIEASR_DIM];   // decoder_proj(decoder(ctx))
    // workspace (bump allocated per call)
    float *ws;
    size_t ws_floats, ws_used, ws_peak;   // ws_peak: the largest use so far
};

// timing per part (pieasr_prof, units of pieasr_clock) and the named traces (pieasr_trace)
#define PQ_PROF_START() uint64_t prof_t0 = pieasr_clock ? pieasr_clock() : 0
#define PQ_PROF_END(field) (pieasr_prof.field += pieasr_clock ? pieasr_clock() - prof_t0 : 0)
#define PQ_TIMED(field, stmt)                                      \
    do {                                                           \
        uint64_t tm0_ = pieasr_clock ? pieasr_clock() : 0;         \
        stmt;                                                      \
        pieasr_prof.field += pieasr_clock ? pieasr_clock() - tm0_ : 0; \
    } while (0)
#define PQ_TRACE(name, p, n)                 \
    do {                                     \
        if (pieasr_trace) {                  \
            pieasr_trace((name), (p), (n));  \
        }                                    \
    } while (0)

static inline float pq_h2f(const uint16_t *p, int i)
{
    return pq_f16_to_f32(p[i]);
}

// element i of a released int8 / uint8 matrix as float
static inline float pq_q8_at(const pieasr_model_t *m, const pq_weights_entry_t *e, int i)
{
    const uint8_t *p = m->weights.base + e->off_a;
    int v = e->bits_or_dtype ? (int)p[i] : (int)(int8_t)p[i];
    return (float)(v - e->zero_point) * e->scale;
}

// model.c: the workspace (bump allocated per call, released by restoring ws_used)
float *pq_ws_alloc(pieasr_model_t *m, size_t n);

// decoder.c
void pq_update_decoder(pieasr_model_t *m);   // m->dec = decoder_proj(decoder(m->ctx))
// the decoder's output for the context (c0, c1) (the last two tokens, 0 = blank) into dec [PIEASR_DIM]; the stream's
// own decoder state is left as it was (the beam search)
void pq_decoder_for(pieasr_model_t *m, int c0, int c1, float *dec);

// joiner.c
// the joiner's tanh (pq_fm_tanh_tab; libm's tanhf differs in the last bits): every search must use it, or near-ties
// between tokens fall differently
float pq_tanh(float x);
void pq_joiner_input(const float *enc, const float *dec, float *u);   // u = tanh(enc + dec), PIEASR_DIM values
// logits [n][PIEASR_VOCAB] of n joiner inputs: the int8 output layer (pieasr_opt_q8 == 2)
void pq_joiner_logits(const pieasr_model_t *m, const float *u, int n, float *lg);
// the same for one input from the released matrix in float (the reference)
void pq_joiner_logits_float(const pieasr_model_t *m, const float *u, float *lg);
int pq_joiner_best(const float *lg);   // the first index of the largest logit

// weights_prepare.c, weights_prepare_int.c: pieasr_init's preparation of the weights
void *pq_alloc16(void *(*alloc)(size_t), size_t n);   // n bytes, 16-byte aligned
int pq_prep_conv_taps(pq_layer_t *ly, int which, int D, int K, void *(*alloc)(size_t));
void pq_rel_pos_table(int L, int S, float *pe);
void pq_conv_edge_scales(const uint16_t *es, int D, int K, int S, float *e);
int pq_prepare_dense(pieasr_model_t *m, void *(*alloc)(size_t));
int pq_prepare_int(pieasr_model_t *m, void *(*alloc)(size_t));
