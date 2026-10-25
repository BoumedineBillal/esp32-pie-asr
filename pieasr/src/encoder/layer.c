// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// One encoder layer: the attention weights, three feed-forwards, the NonlinAttention, two self-attentions, two conv
// modules, bypasses and BiasNorm; with the residual in float or int16.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "encoder/encoder_internal.h"

static char tname[64];   // a trace's name
#define TN(fmt, ...) (snprintf(tname, sizeof(tname), fmt, __VA_ARGS__), tname)

static void add_inplace(float *x, const float *t, int n)
{
    for (int i = 0; i < n; i++) {
        x[i] += t[i];
    }
}

// traces of the int16 residual (value x * s) and of the int8 attention rows (host checks only)
static void trace16(const char *name, const int16_t *x, int n, float s)
{
    if (!pieasr_trace) {
        return;
    }
    float *f = malloc(sizeof(float) * n);
    for (int i = 0; i < n; i++) {
        f[i] = (float)x[i] * s;
    }
    pieasr_trace(name, f, n);
    free(f);
}

static void trace_att8(const char *name, int H, int S, int n)
{
    if (!pieasr_trace || !pq_att8) {
        return;
    }
    float *f = malloc(sizeof(float) * H * S * n);
    for (int r = 0; r < H * S; r++) {
        for (int j = 0; j < n; j++) {
            f[(size_t)r * n + j] = (float)pq_att8[(size_t)r * pq_att8_np + j] * pq_att8_s[r];
        }
    }
    pieasr_trace(name, f, H * S * n);
    free(f);
}

void pq_encoder_layer(pieasr_model_t *m, int si, int li, float *x, const uint8_t *mask)
{
    pq_stack_t *st = &m->stack[si];
    pq_layer_t *ly = &st->layer[li];
    const int S = st->S, D = st->D, n = st->L + S, N = S * D;
    size_t mark = m->ws_used;
#define TRX(k) PQ_TRACE(TN("s%d.l%d.x%d", si, li, (k)), x, N)
    PQ_TRACE(TN("s%d.l%d.in", si, li), x, N);
    float *x_orig = pq_ws_alloc(m, N), *t = pq_ws_alloc(m, N);
    memcpy(x_orig, x, sizeof(float) * N);
    float *attn = pq_ws_alloc(m, (size_t)st->H * S * n);
    if (pieasr_opt_int) {   // the softmax rows int8, shared by NonlinAttention and both self-attentions
        pq_att8_np = (n + 15) & ~15;
        pq_att8 = (int8_t *)(((uintptr_t)pq_ws_alloc(m, ((size_t)st->H * S * pq_att8_np + 16) / 4 + 4) + 15) &
                             ~(uintptr_t)15);
        pq_att8_s = pq_ws_alloc(m, (size_t)st->H * S);
    }
    if (pieasr_opt_int && (pieasr_opt_i16 & PIEASR_I16_RES) && ly->res_s > 0.0f) {
        // the residual int16 (scale res_s) for the layer
        int16_t *x16 = (int16_t *)(((uintptr_t)pq_ws_alloc(m, ((size_t)N + 8) / 2 + 4) + 15) & ~(uintptr_t)15);
        int16_t *xo16 = (int16_t *)(((uintptr_t)pq_ws_alloc(m, ((size_t)N + 8) / 2 + 4) + 15) & ~(uintptr_t)15);
        const float inv = 1.0f / ly->res_s, rs = ly->res_s;
#define TR16(name, sc) (pieasr_trace ? trace16(TN("s%d.l%d.%s", si, li, (name)), x16, N, (sc)) : (void)0)
        for (int i = 0; i < N; i++) {
            const int v = pq_round(x[i] * inv);
            x16[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        memcpy(xo16, x16, sizeof(int16_t) * N);
        m->res16 = x16;
        m->res_j = 0;
        float *xr = (float *)x16;   // the products see the residual through this pointer (pq_io_in / pq_io_out)
        PQ_TIMED(m_attnw, pq_attention_weights(m, st, ly, xr, mask, attn));
        if (pieasr_trace) {
            trace_att8(TN("s%d.l%d.attn", si, li), st->H, S, n);
        }
        PQ_TIMED(m_ff, pq_feed_forward(m, &ly->ff_in[0], &ly->ff_out[0], ly->ff_tab[0], xr, S, xr, 1));
        TR16("x1", rs);
        PQ_TIMED(m_na, pq_nonlin_attention(m, st, ly, xr, attn, xr, 1));
        TR16("x2", rs);
        PQ_TIMED(m_sa, pq_self_attention(m, st, ly, 0, xr, attn, xr, 1));
        TR16("x3", rs);
        PQ_TIMED(m_conv, pq_conv_module(m, st, ly, 0, xr, xr, 1));
        TR16("x4", rs);
        PQ_TIMED(m_ff, pq_feed_forward(m, &ly->ff_in[1], &ly->ff_out[1], ly->ff_tab[1], xr, S, xr, 1));
        TR16("x5", rs);
        pq_bypass16(xo16, x16, S, D, ly->bypass_mid);
        TR16("mid", rs);
        PQ_TIMED(m_sa, pq_self_attention(m, st, ly, 1, xr, attn, xr, 1));
        TR16("x6", rs);
        PQ_TIMED(m_conv, pq_conv_module(m, st, ly, 1, xr, xr, 1));
        TR16("x7", rs);
        m->res_j = ly->res_j;   // x8 = x7 + ff3 at res_s 2^res_j
        PQ_TIMED(m_ff, pq_feed_forward(m, &ly->ff_in[2], &ly->ff_out[2], ly->ff_tab[2], xr, S, xr, 1));
        TR16("x8", rs * (float)(1 << ly->res_j));
        m->res_j = 0;
        PQ_TIMED(m_norm,
                 pq_norm_bypass16(x16, xo16, S, D, ly->norm_bias, ly->norm_scale, ly->bypass, ly->res_s, ly->res_j));
        for (int i = 0; i < N; i++) {
            x[i] = (float)x16[i] * ly->res_s;
        }
        PQ_TRACE(TN("s%d.l%d.out", si, li), x, N);
#undef TR16
        m->res16 = NULL;
        pq_att8 = NULL;
        m->ws_used = mark;
        return;
    }
    PQ_TIMED(m_attnw, pq_attention_weights(m, st, ly, x, mask, attn));
    if (pieasr_opt_int) {
        if (pieasr_trace) {
            trace_att8(TN("s%d.l%d.attn", si, li), st->H, S, n);
        }
    } else {
        PQ_TRACE(TN("s%d.l%d.attn", si, li), attn, st->H * S * n);
    }
    if (pieasr_opt_int) {   // the residual adds fused into each module's last product (x += module(x)); same values
        PQ_TIMED(m_ff, pq_feed_forward(m, &ly->ff_in[0], &ly->ff_out[0], ly->ff_tab[0], x, S, x, 1));
        TRX(1);
        PQ_TIMED(m_na, pq_nonlin_attention(m, st, ly, x, attn, x, 1));
        TRX(2);
        PQ_TIMED(m_sa, pq_self_attention(m, st, ly, 0, x, attn, x, 1));
        TRX(3);
        PQ_TIMED(m_conv, pq_conv_module(m, st, ly, 0, x, x, 1));
        TRX(4);
        PQ_TIMED(m_ff, pq_feed_forward(m, &ly->ff_in[1], &ly->ff_out[1], ly->ff_tab[1], x, S, x, 1));
        TRX(5);
        pq_bypass(x_orig, x, S, D, ly->bypass_mid);
        PQ_TRACE(TN("s%d.l%d.mid", si, li), x, N);
        PQ_TIMED(m_sa, pq_self_attention(m, st, ly, 1, x, attn, x, 1));
        TRX(6);
        PQ_TIMED(m_conv, pq_conv_module(m, st, ly, 1, x, x, 1));
        TRX(7);
        PQ_TIMED(m_ff, pq_feed_forward(m, &ly->ff_in[2], &ly->ff_out[2], ly->ff_tab[2], x, S, x, 1));
        TRX(8);
        PQ_TIMED(m_norm, pq_bias_norm(x, S, D, ly->norm_bias, ly->norm_scale));
        pq_bypass(x_orig, x, S, D, ly->bypass);
        PQ_TRACE(TN("s%d.l%d.out", si, li), x, N);
        pq_att8 = NULL;
        m->ws_used = mark;
        return;
    }
    PQ_TIMED(m_ff, pq_feed_forward(m, &ly->ff_in[0], &ly->ff_out[0], NULL, x, S, t, 0));
    PQ_TRACE(TN("s%d.l%d.ff1", si, li), t, N);
    add_inplace(x, t, N);
    TRX(1);
    PQ_TIMED(m_na, pq_nonlin_attention(m, st, ly, x, attn, t, 0));
    PQ_TRACE(TN("s%d.l%d.na", si, li), t, N);
    add_inplace(x, t, N);
    TRX(2);
    PQ_TIMED(m_sa, pq_self_attention(m, st, ly, 0, x, attn, t, 0));
    PQ_TRACE(TN("s%d.l%d.sa1", si, li), t, N);
    add_inplace(x, t, N);
    TRX(3);
    PQ_TIMED(m_conv, pq_conv_module(m, st, ly, 0, x, t, 0));
    PQ_TRACE(TN("s%d.l%d.conv1", si, li), t, N);
    add_inplace(x, t, N);
    TRX(4);
    PQ_TIMED(m_ff, pq_feed_forward(m, &ly->ff_in[1], &ly->ff_out[1], NULL, x, S, t, 0));
    PQ_TRACE(TN("s%d.l%d.ff2", si, li), t, N);
    add_inplace(x, t, N);
    TRX(5);
    pq_bypass(x_orig, x, S, D, ly->bypass_mid);
    PQ_TRACE(TN("s%d.l%d.mid", si, li), x, N);
    PQ_TIMED(m_sa, pq_self_attention(m, st, ly, 1, x, attn, t, 0));
    PQ_TRACE(TN("s%d.l%d.sa2", si, li), t, N);
    add_inplace(x, t, N);
    TRX(6);
    PQ_TIMED(m_conv, pq_conv_module(m, st, ly, 1, x, t, 0));
    PQ_TRACE(TN("s%d.l%d.conv2", si, li), t, N);
    add_inplace(x, t, N);
    TRX(7);
    PQ_TIMED(m_ff, pq_feed_forward(m, &ly->ff_in[2], &ly->ff_out[2], NULL, x, S, t, 0));
    PQ_TRACE(TN("s%d.l%d.ff3", si, li), t, N);
    add_inplace(x, t, N);
    TRX(8);
    PQ_TIMED(m_norm, pq_bias_norm(x, S, D, ly->norm_bias, ly->norm_scale));
    PQ_TRACE(TN("s%d.l%d.norm", si, li), x, N);
    pq_bypass(x_orig, x, S, D, ly->bypass);
    PQ_TRACE(TN("s%d.l%d.out", si, li), x, N);
#undef TRX
    m->ws_used = mark;
}
