// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// pieasr_init's int8 copies: the depthwise taps, the dense front-end convs and joiner, the position tables; the
// in-place region allocator that writes them over their sources.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "encoder/encoder_internal.h"
#include "math/detmath.h"
#include "math/tables/tables.h"
#include "model_internal.h"

// In-place conversions (pieasr_opt_inplace, for the int8 path only): the int8 copies of weights are written over
// their float16 / int8 sources in the weights buffer, which that path never reads again; what does not fit there
// comes from the real allocator. rg_alloc hands out the current source region.
static uint8_t *rg_ptr, *rg_end;
static void *(*rg_fallback)(size_t);

static void rg_set(const void *src, size_t bytes)
{
    rg_ptr = pieasr_opt_inplace ? (uint8_t *)src : NULL;
    rg_end = pieasr_opt_inplace ? (uint8_t *)src + bytes : NULL;
}

static void *rg_alloc(size_t n)
{
    if (rg_ptr) {
        uint8_t *p = (uint8_t *)(((uintptr_t)rg_ptr + 15) & ~(uintptr_t)15);
        if (p + n <= rg_end) {
            rg_ptr = p + n;
            return p;
        }
    }
    return rg_fallback(n);
}

void *pq_alloc16(void *(*alloc)(size_t), size_t n)
{
    uint8_t *p = alloc(n + 16);
    return p ? (void *)(((uintptr_t)p + 15) & ~(uintptr_t)15) : NULL;
}

// int8 taps [D/16][taps][16] with one scale per channel (max |w| / 127), from float16 [D][taps] (read into a
// float copy first: the result may overwrite the source)
static int8_t *taps_q8(const uint16_t *w, int D, int taps, float *scale, void *(*alloc)(size_t))
{
    float *wf = malloc(sizeof(float) * D * taps);
    if (!wf) {
        return NULL;
    }
    pq_f16_to_f32_vec(w, wf, D * taps);
    int8_t *q = alloc((size_t)D * taps + 16);
    if (!q) {
        free(wf);
        return NULL;
    }
    q = (int8_t *)(((uintptr_t)q + 15) & ~(uintptr_t)15);
    for (int d = 0; d < D; d++) {
        float amax = 0.0f;
        for (int k = 0; k < taps; k++) {
            amax = fabsf(wf[d * taps + k]) > amax ? fabsf(wf[d * taps + k]) : amax;
        }
        scale[d] = amax / 127.0f;
        const float inv = amax > 0.0f ? 127.0f / amax : 0.0f;
        for (int k = 0; k < taps; k++) {
            q[((size_t)(d / 16) * taps + k) * 16 + (d & 15)] = (int8_t)pq_round(wf[d * taps + k] * inv);
        }
    }
    free(wf);
    return q;
}

int pq_prep_conv_taps(pq_layer_t *ly, int which, int D, int K, void *(*alloc)(size_t))
{
    const int lp = K / 2;
    float *f = alloc(sizeof(float) * 4 * D);
    if (!f) {
        return -1;
    }
    ly->cv_swc[which] = f;
    ly->cv_swk[which] = f + D;
    ly->cv_bcf[which] = f + 2 * D;
    ly->cv_bkf[which] = f + 3 * D;
    rg_fallback = alloc;
    rg_set(ly->cv_causal_w[which], (size_t)D * (lp + 1) * 2);
    ly->cv_wc8[which] = taps_q8(ly->cv_causal_w[which], D, lp + 1, ly->cv_swc[which], rg_alloc);
    rg_set(ly->cv_chunk_w[which], (size_t)D * K * 2);
    ly->cv_wk8[which] = taps_q8(ly->cv_chunk_w[which], D, K, ly->cv_swk[which], rg_alloc);
    rg_set(NULL, 0);
    pq_f16_to_f32_vec(ly->cv_causal_b[which], ly->cv_bcf[which], D);
    pq_f16_to_f32_vec(ly->cv_chunk_b[which], ly->cv_bkf[which], D);
    return ly->cv_wc8[which] && ly->cv_wk8[which] ? 0 : -1;
}

void pq_rel_pos_table(int L, int S, float *pe)
{
    const int n = L + 2 * S - 1;
    const float cl = sqrtf((float)POS_DIM);
    const float ls = (float)(1.0 * POS_DIM / (2.0 * M_PI));
    for (int r = 0; r < n; r++) {
        float x = (float)(r - (L + S - 1));
        float sg = x > 0 ? 1.0f : x < 0 ? -1.0f : 0.0f;
        // deterministic math (detmath.h): the same table on the host and the chip
        float xc = cl * sg * ((float)dm_log((double)(fabsf(x) + cl)) - (float)dm_log((double)cl));
        float a = (float)dm_atan((double)(xc / ls));
        for (int i = 0; i < POS_DIM / 2; i++) {
            double sn, cs;
            dm_sincos((double)(a * (float)(i + 1)), &sn, &cs);
            pe[r * POS_DIM + 2 * i] = (float)cs;
            pe[r * POS_DIM + 2 * i + 1] = (float)sn;
        }
        pe[r * POS_DIM + POS_DIM - 1] = 1.0f;
    }
}

// the chunk edge scale 1 + (left + right) [D][S] from the float16 chunkwise_conv_scale [2][D][K]
void pq_conv_edge_scales(const uint16_t *es, int D, int K, int S, float *e)
{
    for (int d = 0; d < D; d++) {
        for (int t = 0; t < S; t++) {
            float left, right;
            if (S < K) {
                left = pq_h2f(es, d * K + t);
                right = pq_h2f(es, D * K + d * K + (K - S) + t);
            } else {
                left = t < K ? pq_h2f(es, d * K + t) : 0.0f;
                right = t >= S - K ? pq_h2f(es, D * K + d * K + t - (S - K)) : 0.0f;
            }
            e[d * S + t] = 1.0f + (left + right);
        }
    }
}

// the int8 path's weights: dense int8 copies of the front-end convs, the released int8 joiner matrices, the ConvNeXt
// depthwise taps
int pq_prepare_dense(pieasr_model_t *m, void *(*alloc)(size_t))
{
    int r = 0;
    float *tmp = malloc(sizeof(float) * 384 * 128), *bias = malloc(sizeof(float) * 650);
    if (!tmp || !bias) {
        return -3;
    }
    rg_fallback = alloc;
    // (pieasr_opt_inplace: each int8 copy over its float16 source, which tmp has already read)
    pq_f16_to_f32_vec(m->conv4_w, tmp, 32 * 72);
    pq_f16_to_f32_vec(m->conv4_b, bias, 32);
    rg_set(m->conv4_w, 32 * 72 * 2);
    r |= pq_dense_from_f32(&m->conv4_q, tmp, 32, 72, 72, bias, rg_alloc);
    pq_f16_to_f32_vec(m->conv7_w, tmp, 128 * 288);
    pq_f16_to_f32_vec(m->conv7_b, bias, 128);
    rg_set(m->conv7_w, 128 * 288 * 2);
    r |= pq_dense_from_f32(&m->conv7_q, tmp, 128, 288, 288, bias, rg_alloc);
    pq_f16_to_f32_vec(m->pw1_w, tmp, 384 * 128);
    pq_f16_to_f32_vec(m->pw1_b, bias, 384);
    rg_set(m->pw1_w, 384 * 128 * 2);
    r |= pq_dense_from_f32(&m->pw1_q, tmp, 384, 128, 128, bias, rg_alloc);
    pq_f16_to_f32_vec(m->pw2_w, tmp, 128 * 384);
    pq_f16_to_f32_vec(m->pw2_b, bias, 128);
    if (pieasr_opt_int) {
        // the hidden's per-channel int8 scales s_k folded into pw2's columns: w'[o][k] = w[o][k] s_k
        const float *amax = NULL;
        for (int i = 0; i < PQ_NACTCH; i++) {
            if (!strcmp(pq_actch[i].name, "fe.pw1") && pq_actch[i].n == 384) {
                amax = pq_actch[i].amax;
            }
        }
        m->pw1_is = alloc(sizeof(float) * 384);
        if (!amax || !m->pw1_is) {
            return -9;
        }
        for (int k = 0; k < 384; k++) {
            const float sk = amax[k] / 127.0f;
            m->pw1_is[k] = amax[k] > 0.0f ? 127.0f / amax[k] : 0.0f;
            for (int o = 0; o < 128; o++) {
                tmp[o * 384 + k] *= sk;
            }
        }
    }
    rg_set(m->pw2_w, 128 * 384 * 2);
    r |= pq_dense_from_f32(&m->pw2_q, tmp, 128, 384, 384, bias, rg_alloc);
    pq_f16_to_f32_vec(m->dec_proj_b, bias, PIEASR_DIM);
    {   // the released int8 matrix, copied first (the tiles may overwrite it)
        const uint8_t *src = m->weights.base + m->dec_proj->off_a;
        uint8_t *copy = malloc(PIEASR_DIM * PIEASR_DIM);
        if (!copy) {
            free(tmp);
            free(bias);
            return -3;
        }
        memcpy(copy, src, PIEASR_DIM * PIEASR_DIM);
        // PQ_Q8_SLACK: the weights file left room for the tiled copy after the source
        rg_set(src,
               (m->dec_proj->flags & PQ_Q8_SLACK) ? pq_dense_bytes(PIEASR_DIM, PIEASR_DIM) : PIEASR_DIM * PIEASR_DIM);
        r |= pq_dense_from_q8(&m->dec_proj_q, copy, m->dec_proj->bits_or_dtype, m->dec_proj->flags & 1, PIEASR_DIM,
                              PIEASR_DIM, m->dec_proj->scale, m->dec_proj->zero_point, bias, rg_alloc);
        free(copy);
    }
    rg_set(NULL, 0);
    pq_f16_to_f32_vec(m->out_lin_b, bias, PIEASR_VOCAB);
    if (pieasr_opt_inplace && (m->out_lin->flags & PQ_Q8_SLACK)) {   // the joiner's tiles over its source too
        const uint8_t *src = m->weights.base + m->out_lin->off_a;
        uint8_t *copy = malloc((size_t)PIEASR_VOCAB * PIEASR_DIM);
        if (!copy) {
            free(tmp);
            free(bias);
            return -3;
        }
        memcpy(copy, src, (size_t)PIEASR_VOCAB * PIEASR_DIM);
        rg_set(src, pq_dense_bytes(PIEASR_VOCAB, PIEASR_DIM));
        r |= pq_dense_from_q8(&m->out_lin_q, copy, m->out_lin->bits_or_dtype, m->out_lin->flags & 1, PIEASR_VOCAB,
                              PIEASR_DIM, m->out_lin->scale, m->out_lin->zero_point, bias, rg_alloc);
        rg_set(NULL, 0);
        free(copy);
    } else {
        r |= pq_dense_from_q8(&m->out_lin_q, m->weights.base + m->out_lin->off_a, m->out_lin->bits_or_dtype,
                              m->out_lin->flags & 1, PIEASR_VOCAB, PIEASR_DIM, m->out_lin->scale,
                              m->out_lin->zero_point, bias, alloc);
    }
    m->dw_wf = alloc(sizeof(float) * 128 * 49);
    if (r || !m->dw_wf) {
        free(tmp);
        free(bias);
        return -4;
    }
    pq_f16_to_f32_vec(m->dw_w, m->dw_wf, 128 * 49);
    // the ConvNeXt depthwise taps as int8 [8][49][16] (over their float16 source when pieasr_opt_inplace)
    m->dw_sw = alloc(sizeof(float) * 256);
    if (!m->dw_sw) {
        free(tmp);
        free(bias);
        return -4;
    }
    m->dw_bf = m->dw_sw + 128;
    pq_f16_to_f32_vec(m->dw_b, m->dw_bf, 128);
    rg_set(m->dw_w, 128 * 49 * 2);
    m->dw_w8 = taps_q8(m->dw_w, 128, 49, m->dw_sw, rg_alloc);
    rg_set(NULL, 0);
    for (int ky = 0; ky < 7; ky++) {
        for (int kx = 0; kx < 7; kx++) {
            pq_cnx_off[ky * 7 + kx] = (ky * 25 + kx) * 128;
        }
    }
    free(tmp);
    free(bias);
    return 0;
}
