// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// What the encoder's files share: the per-core working buffers, the layer's int8 softmax rows, the modules.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "kernels/kernels.h"
#include "model_internal.h"
#include "runtime/parallel.h"

// the int8 buffers of the attention products (pieasr_opt_q8 == 2), per core; allocated in pieasr_init
typedef struct {
    int8_t *xq, *vt;   // [S][np], [np][16] + 32
    float *xs;         // [S]
    int32_t *acc;      // [S][16]
    int16_t *v16;      // PIEASR_I16_AW: the scores of one head [S][np]
} pq_av_buf_t;
extern pq_av_buf_t pq_avq[PQ_PAR_PARTS];

// pieasr_opt_int: the current layer's attention weights as int8 rows (the softmax quantized once, in the scores):
// row (h, t) at pq_att8 + (h * S + t) * pq_att8_np (zero padded to pq_att8_np, a multiple of 16), its scale
// pq_att8_s[h * S + t]; NULL outside a layer
extern int8_t *pq_att8;
extern float *pq_att8_s;
extern int pq_att8_np;

// the int8 buffers of one 16-channel block of the conv module's depthwise convolutions, per core; allocated in
// pieasr_init
typedef struct {
    int8_t *xc, *gz;          // [lp + S][16], [lp + S + lp][16]
    int32_t *accc, *acck;     // [S][16]
    float *gb;                // [lp + S][16]: cache then GLU outputs, float
    int16_t *g16;             // PIEASR_I16_CV: [lp + S][16] int16
    pq_comb_t *comb;          // its combine parameters (16-byte aligned)
} pq_dw_buf_t;
extern pq_dw_buf_t pq_dwq[PQ_PAR_PARTS];
extern int32_t pq_dw_off[32];    // tap k at 16 k bytes
extern int32_t pq_cnx_off[49];   // the ConvNeXt depthwise taps' offsets in its int8 input (set in pieasr_init)

// encoder.c: y [T, out] = x [T, in] W^T + b (2/4-bit), the product chosen by the options
void pq_linear(pieasr_model_t *m, const pq_lin_t *l, const float *x, int T, float *y);
// pq_linear, then post (PQI_POST_*): fused into the integer product's output loop when there is one
void pq_linear_p(pieasr_model_t *m, const pq_lin_t *l, const float *x, int T, float *y, int post);
// the integer product's input / output: the layer's int16 residual (m->res16, passed as a float pointer) or floats
void pq_io_in(const pieasr_model_t *m, pqi_io_t *io, const float *x);
void pq_io_out(const pieasr_model_t *m, pqi_io_t *io, float *y, int post);

// front_end.c: x [141, 80] -> y [64, 192]; updates left_pad (pq_front_end_q8: the convolutions as int8 products)
void pq_front_end(pieasr_model_t *m, const float *x, float *y);
void pq_front_end_q8(pieasr_model_t *m, const float *x, float *y);

// layer.c: layer li of stack si on x [S, D] in place
void pq_encoder_layer(pieasr_model_t *m, int si, int li, float *x, const uint8_t *mask);

// feed_forward.c: acc: y += the module's output (pieasr_opt_int: fused into the last product; x and y may then be the
// same buffer)
void pq_feed_forward(pieasr_model_t *m, const pq_lin_t *in, const pq_lin_t *out, const uint32_t *tab, const float *x,
                     int S, float *y, int acc);

// attention_weights.c: attn [H, S, L+S] (or pq_att8); updates the key cache
void pq_attention_weights(pieasr_model_t *m, const pq_stack_t *st, pq_layer_t *ly, const float *x, const uint8_t *mask,
                          float *attn);
// attention_values.c
void pq_nonlin_attention(pieasr_model_t *m, const pq_stack_t *st, pq_layer_t *ly, const float *x, const float *attn0,
                         float *y, int acc);
void pq_self_attention(pieasr_model_t *m, const pq_stack_t *st, pq_layer_t *ly, int which, const float *x,
                       const float *attn, float *y, int acc);

// conv_module.c
void pq_conv_module(pieasr_model_t *m, const pq_stack_t *st, pq_layer_t *ly, int which, const float *x, float *y,
                    int acc);

// norm.c
void pq_bias_norm(float *x, int T, int C, const uint16_t *bias, float scale);
void pq_bypass(const float *x_orig, float *x, int T, int C, const uint16_t *s);
void pq_bypass16(const int16_t *xo, int16_t *x, int T, int C, const uint16_t *s);
void pq_norm_bypass16(int16_t *x, const int16_t *xo, int T, int C, const uint16_t *bias, float scale,
                      const uint16_t *byp, float s, int j);
