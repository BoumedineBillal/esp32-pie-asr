// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The encoder: one chunk of features through the front end, the six stacks (downsampling, layers, combiner) and the
// output projection; the 2/4-bit products' dispatch (float, int8 or integer).
#include <stdio.h>
#include <string.h>
#include "encoder/encoder_internal.h"
#include "math/fastmath.h"

static char tname[64];   // a trace's name
#define TN(fmt, ...) (snprintf(tname, sizeof(tname), fmt, __VA_ARGS__), tname)

void pq_io_in(const pieasr_model_t *m, pqi_io_t *io, const float *x)
{
    if (m->res16 && x == (const float *)m->res16) {
        io->x16 = m->res16;
    } else {
        io->x = x;
    }
}

void pq_io_out(const pieasr_model_t *m, pqi_io_t *io, float *y, int post)
{
    if (m->res16 && y == (float *)m->res16 && post == PQI_POST_ADD) {
        io->y16 = m->res16;
        io->post = PQI_POST_ADD16;
        io->res_j = m->res_j;
    } else {
        io->y = y;
        io->post = post;
    }
}

void pq_linear(pieasr_model_t *m, const pq_lin_t *l, const float *x, int T, float *y)
{
    size_t mark = m->ws_used;
    PQ_PROF_START();
    if (pieasr_opt_int && l->qi.n16) {
        pqi_io_t io = { .T = T, .ldx = l->w.in, .ldy = l->w.out,
                        .scratch = pq_ws_alloc(m, (pq_linear_int_scratch(&l->w, T) + 3) / 4) };
        pq_io_in(m, &io, x);
        pq_io_out(m, &io, y, PQI_POST_NONE);
        pq_linear_int_io(&l->w, &l->qi, l->b, &io);
    } else if (pieasr_opt_q8 == 2) {
        void *scratch = pq_ws_alloc(m, (pq_linear_pie_scratch(&l->w, T) + 3) / 4);
        pq_linear_pie(&l->w, l->b, x, T, l->w.in, y, l->w.out, scratch);
    } else if (pieasr_opt_q8) {
        void *scratch = pq_ws_alloc(m, (pq_linear_q8_scratch(&l->w, T) + 3) / 4);
        pq_linear_q8(&l->w, l->b, x, T, l->w.in, y, l->w.out, scratch);
    } else {
        float *scratch = pq_ws_alloc(m, (size_t)T * l->w.in);
        pq_linear_ref(&l->w, l->b, x, T, l->w.in, y, l->w.out, scratch);
    }
    PQ_PROF_END(linear);
    m->ws_used = mark;
}

void pq_linear_p(pieasr_model_t *m, const pq_lin_t *l, const float *x, int T, float *y, int post)
{
    size_t mark = m->ws_used;
    if (pieasr_opt_int && l->qi.n16) {
        PQ_PROF_START();
        pqi_io_t io = { .T = T, .ldx = l->w.in, .ldy = l->w.out,
                        .scratch = pq_ws_alloc(m, (pq_linear_int_scratch(&l->w, T) + 3) / 4) };
        pq_io_in(m, &io, x);
        pq_io_out(m, &io, y, post);
        pq_linear_int_io(&l->w, &l->qi, l->b, &io);
        PQ_PROF_END(linear);
        m->ws_used = mark;
        return;
    }
    const int n = T * l->w.out;
    if (post == PQI_POST_ADD) {
        float *t = pq_ws_alloc(m, n);
        pq_linear(m, l, x, T, t);
        for (int i = 0; i < n; i++) {
            y[i] += t[i];
        }
    } else {
        pq_linear(m, l, x, T, y);
        if (post == PQI_POST_SWOOSH_L) {
            for (int i = 0; i < n; i++) {
                y[i] = pq_fm_swoosh_l_tab(y[i]);
            }
        }
    }
    m->ws_used = mark;
}

void pieasr_encode_chunk(pieasr_model_t *m, const float *x, float *enc)
{
    PQ_PROF_START();
    m->ws_used = 0;
    const int T = PQ_EMBED_T;
    float *h = pq_ws_alloc(m, (size_t)T * PIEASR_DIM);         // stack input/output [64, D]
    if (pieasr_opt_q8 == 2) {
        PQ_TIMED(front, pq_front_end_q8(m, x, h));
    } else {
        PQ_TIMED(front, pq_front_end(m, x, h));
    }
    // key padding mask at 50 Hz [320]: left position j masked iff processed <= 255 - j
    uint8_t mask[PQ_LEFT + PQ_EMBED_T];
    for (int j = 0; j < PQ_LEFT + PQ_EMBED_T; j++) {
        mask[j] = j < PQ_LEFT && m->processed <= PQ_LEFT - 1 - j;
    }
    float *outs = pq_ws_alloc(m, (size_t)T * PIEASR_DIM);      // full [64, 512]: s5 0..255, s4 256..383, s3 384..511
    int Dprev = 192;
    for (int si = 0; si < PQ_STACKS; si++) {
        pq_stack_t *st = &m->stack[si];
        const int D = st->D, ds = st->ds, S = st->S;
        size_t mark = m->ws_used;
        float *xin = pq_ws_alloc(m, (size_t)T * D);
        for (int t = 0; t < T; t++) {   // convert_num_channels
            for (int c = 0; c < D; c++) {
                xin[(size_t)t * D + c] = c < Dprev ? h[(size_t)t * Dprev + c] : 0.0f;
            }
        }
        PQ_TRACE(TN("s%d.in", si), xin, T * D);
        float *xs = pq_ws_alloc(m, (size_t)S * D);
        if (ds > 1) {
            for (int t = 0; t < S; t++) {
                for (int c = 0; c < D; c++) {
                    float a = 0.0f;
                    for (int i = 0; i < ds; i++) {
                        a += st->ds_w[i] * xin[(size_t)(t * ds + i) * D + c];
                    }
                    xs[(size_t)t * D + c] = a;
                }
            }
            PQ_TRACE(TN("s%d.ds", si), xs, S * D);
        } else {
            memcpy(xs, xin, sizeof(float) * T * D);
        }
        uint8_t smask[PQ_LEFT + PQ_EMBED_T];
        for (int j = 0; j < st->L + S; j++) {
            smask[j] = mask[j * ds];
        }
        for (int li = 0; li < st->layers; li++) {
            PQ_TIMED(layers, pq_encoder_layer(m, si, li, xs, smask));
        }
        // stack output [64, D] into h
        float ocf[512];   // out_combiner scales as floats once (D <= 512)
        for (int c = 0; c < D && ds > 1; c++) {
            ocf[c] = pq_h2f(st->out_combiner, c);
        }
        for (int t = 0; t < T; t++) {
            for (int c = 0; c < D; c++) {
                size_t i = (size_t)t * D + c;
                if (ds > 1) {
                    float up = xs[(size_t)(t / ds) * D + c];
                    h[i] = xin[i] + (up - xin[i]) * ocf[c];
                } else {
                    h[i] = xs[i];
                }
            }
        }
        PQ_TRACE(TN("s%d.out", si), h, T * D);
        for (int t = 0; t < T; t++) {
            int lo = si == 5 ? 0 : si == 4 ? 256 : si == 3 ? 384 : -1;
            int hi = si == 5 ? 256 : si == 4 ? 384 : 512;
            for (int c = lo; lo >= 0 && c < hi; c++) {
                outs[(size_t)t * PIEASR_DIM + c] = h[(size_t)t * D + c];
            }
        }
        Dprev = D;
        m->ws_used = mark;
    }
    PQ_TRACE("enc.full", outs, T * PIEASR_DIM);
    float *y = pq_ws_alloc(m, (size_t)PIEASR_OUT_T * PIEASR_DIM);
    for (int t = 0; t < PIEASR_OUT_T; t++) {
        for (int c = 0; c < PIEASR_DIM; c++) {
            y[(size_t)t * PIEASR_DIM + c] = m->out_ds_w[0] * outs[(size_t)(2 * t) * PIEASR_DIM + c] +
                                            m->out_ds_w[1] * outs[(size_t)(2 * t + 1) * PIEASR_DIM + c];
        }
    }
    PQ_TRACE("enc.ds", y, PIEASR_OUT_T * PIEASR_DIM);
    pq_linear(m, &m->enc_proj, y, PIEASR_OUT_T, enc);
    PQ_TRACE("enc.out", enc, PIEASR_OUT_T * PIEASR_DIM);
    m->processed = m->processed + PQ_EMBED_T > PQ_LEFT ? PQ_LEFT : m->processed + PQ_EMBED_T;
    m->ws_used = 0;
    PQ_PROF_END(total);
}
