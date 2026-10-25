// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The front end (encoder_embed): three 3x3 convolutions, the ConvNeXt block, the output projection and BiasNorm;
// in float, and with the convolutions as int8 products.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "encoder/encoder_internal.h"
#include "math/fastmath.h"

// cross-correlation, x [Cin, H, W], w f16 [Cout, Cin, 3, 3], zero padding -> y [Cout, Ho, Wo], then swoosh_r
static void conv3x3_swoosh_r(const float *x, int Cin, int H, int W, const uint16_t *w, const uint16_t *b, int Cout,
                             int sh, int sw, int ph, int pw, float *y, int *Ho_, int *Wo_)
{
    const int Ho = (H + 2 * ph - 3) / sh + 1, Wo = (W + 2 * pw - 3) / sw + 1;
    for (int co = 0; co < Cout; co++) {
        float wk[32 * 9];
        for (int i = 0; i < Cin * 9; i++) {
            wk[i] = pq_h2f(w, co * Cin * 9 + i);
        }
        const float bias = pq_h2f(b, co);
        for (int oy = 0; oy < Ho; oy++) {
            for (int ox = 0; ox < Wo; ox++) {
                float acc = 0.0f;
                for (int ci = 0; ci < Cin; ci++) {
                    for (int ky = 0; ky < 3; ky++) {
                        int iy = oy * sh + ky - ph;
                        if (iy < 0 || iy >= H) {
                            continue;
                        }
                        for (int kx = 0; kx < 3; kx++) {
                            int ix = ox * sw + kx - pw;
                            if (ix < 0 || ix >= W) {
                                continue;
                            }
                            acc += wk[(ci * 3 + ky) * 3 + kx] * x[((size_t)ci * H + iy) * W + ix];
                        }
                    }
                }
                y[((size_t)co * Ho + oy) * Wo + ox] = pq_fm_swoosh_r_tab(acc + bias);
            }
        }
    }
    *Ho_ = Ho;
    *Wo_ = Wo;
}

// x [141, 80] -> y [64, 192]; updates left_pad
void pq_front_end(pieasr_model_t *m, const float *x, float *y)
{
    size_t mark = m->ws_used;
    int H, W, H2, W2, H3, W3;
    PQ_PROF_START();
    float *h3 = pq_ws_alloc(m, 128 * 67 * 19);
    size_t mark2 = m->ws_used;   // h1 and h2 are freed once h3 is done
    float *h1 = pq_ws_alloc(m, 8 * 139 * 80);
    // [8,139,80]
    conv3x3_swoosh_r(x, 1, PIEASR_CHUNK_IN, PIEASR_MEL, m->conv0_w, m->conv0_b, 8, 1, 1, 0, 1, h1, &H, &W);
    PQ_TRACE("embed.c0", h1, 8 * H * W);
    float *h2 = pq_ws_alloc(m, 32 * 69 * 39);
    conv3x3_swoosh_r(h1, 8, H, W, m->conv4_w, m->conv4_b, 32, 2, 2, 0, 0, h2, &H2, &W2);       // [32,69,39]
    PQ_TRACE("embed.c4", h2, 32 * H2 * W2);
    conv3x3_swoosh_r(h2, 32, H2, W2, m->conv7_w, m->conv7_b, 128, 1, 2, 0, 0, h3, &H3, &W3);   // [128,67,19]
    m->ws_used = mark2;
    PQ_PROF_END(front_conv);
    PQ_TRACE("embed.conv", h3, 128 * 67 * 19);
    prof_t0 = pieasr_clock ? pieasr_clock() : 0;
    // ConvNeXt: hc = concat(left_pad [128,3,19], h3 [128,67,19]) over time -> [128, 70, 19]
    const int T = PQ_EMBED_T, F = 19, C = 128, TC = 70;
    float *e = pq_ws_alloc(m, (size_t)T * C * F);   // the ConvNeXt output, [64, 128*19]
    size_t mark3 = m->ws_used;                    // hc is freed before the output product
    float *hc = pq_ws_alloc(m, (size_t)C * TC * F);
    for (int c = 0; c < C; c++) {
        memcpy(hc + (size_t)c * TC * F, m->left_pad + (size_t)c * 3 * F, sizeof(float) * 3 * F);
        memcpy(hc + ((size_t)c * TC + 3) * F, h3 + (size_t)c * 67 * F, sizeof(float) * 67 * F);
    }
    // new left pad = hc[:, 64:67]
    for (int c = 0; c < C; c++) {
        memcpy(m->left_pad + (size_t)c * 3 * F, hc + ((size_t)c * TC + T) * F, sizeof(float) * 3 * F);
    }
    // per output frame t: depthwise 7x7 (pad time 0, freq 3) -> d [128, 19]; then pointwise 128 -> 384 (swoosh_l)
    // -> 128, + bypass h3[:, t]; written transposed into e [64, 128*19] (column c*19 + f)
    float hid[384], in[128], d[128 * 19];
    for (int t = 0; t < T; t++) {
        for (int c = 0; c < C; c++) {
            const uint16_t *wk = m->dw_w + c * 49;
            const float bias = pq_h2f(m->dw_b, c);
            for (int f = 0; f < F; f++) {
                float acc = 0.0f;
                for (int ky = 0; ky < 7; ky++) {
                    for (int kx = 0; kx < 7; kx++) {
                        int ix = f + kx - 3;
                        if (ix < 0 || ix >= F) {
                            continue;
                        }
                        acc += pq_h2f(wk, ky * 7 + kx) * hc[((size_t)c * TC + t + ky) * F + ix];
                    }
                }
                d[c * F + f] = acc + bias;
            }
        }
        for (int f = 0; f < F; f++) {
            for (int c = 0; c < C; c++) {
                in[c] = d[c * F + f];
            }
            for (int o = 0; o < 384; o++) {
                float acc = 0.0f;
                for (int c = 0; c < C; c++) {
                    acc += pq_h2f(m->pw1_w, o * C + c) * in[c];
                }
                hid[o] = pq_fm_swoosh_l_tab(acc + pq_h2f(m->pw1_b, o));
            }
            for (int o = 0; o < C; o++) {
                float acc = 0.0f;
                for (int k = 0; k < 384; k++) {
                    acc += pq_h2f(m->pw2_w, o * 384 + k) * hid[k];
                }
                e[(size_t)t * C * F + o * F + f] = h3[((size_t)o * 67 + t) * F + f] + acc + pq_h2f(m->pw2_b, o);
            }
        }
    }
    PQ_PROF_END(front_convnext);
    if (pieasr_trace) {   // embed.convnext as [128, 64, 19] (host checks only)
        float *tr = malloc(sizeof(float) * C * T * F);
        for (int t = 0; t < T; t++) {
            for (int c = 0; c < C; c++) {
                for (int f = 0; f < F; f++) {
                    tr[((size_t)c * T + t) * F + f] = e[(size_t)t * C * F + c * F + f];
                }
            }
        }
        PQ_TRACE("embed.convnext", tr, C * T * F);
        free(tr);
    }
    m->ws_used = mark3;
    pq_linear(m, &m->embed_out, e, T, y);
    PQ_TRACE("embed.linear", y, T * 192);
    pq_bias_norm(y, T, 192, m->out_norm_bias, m->out_norm_scale);
    PQ_TRACE("embed.out", y, T * 192);
    m->ws_used = mark;
}

// the same front end with conv.4, conv.7 and the ConvNeXt convs as int8 products (the tile and depthwise kernels)

// conv.0 on the zero-padded input xp [141][82]: output channels split between the cores
typedef struct {
    pieasr_model_t *m;
    const float *xp;
    float *y;
} conv0_job_t;

static void conv0_part(void *ctx, int part, int parts)
{
    const conv0_job_t *j = ctx;
    const int H = PIEASR_CHUNK_IN - 2, W = PIEASR_MEL, WP = PIEASR_MEL + 2;
    int c0, c1;
    pq_par_range(8, part, parts, 1, &c0, &c1);
    for (int co = c0; co < c1; co++) {
        float wk[9];
        for (int i = 0; i < 9; i++) {
            wk[i] = pq_h2f(j->m->conv0_w, co * 9 + i);
        }
        const float bias = pq_h2f(j->m->conv0_b, co);
        for (int oy = 0; oy < H; oy++) {
            const float *r0 = j->xp + oy * WP, *r1 = r0 + WP, *r2 = r1 + WP;
            float *out = j->y + ((size_t)co * H + oy) * W;
            // the 80 outputs of the row are independent: 4 at a time (the same expression per output)
            for (int ox = 0; ox < W; ox += 4) {
                float a[4];
                for (int u = 0; u < 4; u++) {
                    const int q = ox + u;
                    float acc = wk[0] * r0[q] + wk[1] * r0[q + 1] + wk[2] * r0[q + 2];
                    acc += wk[3] * r1[q] + wk[4] * r1[q + 1] + wk[5] * r1[q + 2];
                    acc += wk[6] * r2[q] + wk[7] * r2[q + 1] + wk[8] * r2[q + 2];
                    a[u] = acc;
                }
                for (int u = 0; u < 4; u++) {
                    out[ox + u] = pq_fm_swoosh_r_tab(a[u] + bias);
                }
            }
        }
    }
}

// the ConvNeXt depthwise 7x7 of output frame t, channels split between the cores: d [19][128]. Its input is
// concat(left_pad [128, 3, 19], h3 [128, 67, 19]) over time.
typedef struct {
    pieasr_model_t *m;
    const float *h3;
    float *d;      // [nt * 19][128]
    int t, nt;     // output frames t .. t + nt - 1
    const int8_t *x8;   // the int8 input (cnx_quant_part), or NULL for the float loops
    const float *sx;
} cnx_job_t;

// The int8 version (pieasr_opt_q8 == 2): the ConvNeXt input concat(left_pad, h3) as int8 x8 [70][25][128]
// (frequency zero-padded by 3 on both sides, channels last, one scale per channel = max over the 70 x 19 values /
// 127); the 49 taps int8 per channel (m->dw_w8, in 16-channel blocks); pq_dw16 per 16 channels and output frame (19
// outputs).
int32_t pq_cnx_off[49];
static int32_t cnx_acc[PQ_PAR_PARTS][19 * 16] __attribute__((aligned(16)));

typedef struct {
    pieasr_model_t *m;
    const float *h3;
    int8_t *x8;
    float *sx;   // [128]
} cnx_q_job_t;

static void cnx_quant_part(void *ctx, int part, int parts)
{
    const cnx_q_job_t *jb = ctx;
    const pieasr_model_t *m = jb->m;
    const int F = 19, C = 128, T3 = 67, FP = 25;
    int c0, c1;
    pq_par_range(C, part, parts, 16, &c0, &c1);
    for (int c = c0; c < c1; c++) {
        const float *lp = m->left_pad + (size_t)c * 3 * F, *h = jb->h3 + (size_t)c * T3 * F;
        float mx = 0.0f;
        for (int i = 0; i < 3 * F; i++) {
            mx = pq_fmax(mx, fabsf(lp[i]));
        }
        for (int i = 0; i < T3 * F; i++) {
            mx = pq_fmax(mx, fabsf(h[i]));
        }
        jb->sx[c] = mx / 127.0f;
        const float inv = mx > 0.0f ? 127.0f / mx : 0.0f;
        for (int ti = 0; ti < 3 + T3; ti++) {
            const float *r = ti < 3 ? lp + ti * F : h + (ti - 3) * F;
            int8_t *o = jb->x8 + ((size_t)ti * FP + 3) * C + c;
            for (int f = 0; f < F; f++) {
                o[(size_t)f * C] = (int8_t)pq_round(r[f] * inv);
            }
        }
    }
}

static void cnx_dw_part_q8(const cnx_job_t *jb, int part, int parts)
{
    const pieasr_model_t *m = jb->m;
    const int F = 19, C = 128, FP = 25;
    int32_t *acc = cnx_acc[part];
    int b0, b1;
    pq_par_range(C / 16, part, parts, 1, &b0, &b1);
    for (int cb = b0; cb < b1; cb++) {
        for (int tt = 0; tt < jb->nt; tt++) {
            pq_dw16(jb->x8 + (size_t)(jb->t + tt) * FP * C + cb * 16, C, F, pq_cnx_off, m->dw_w8 + cb * 49 * 16, 49,
                    acc);
            for (int f = 0; f < F; f++) {
                float *d = jb->d + (size_t)(tt * F + f) * C + cb * 16;
                for (int i = 0; i < 16; i++) {
                    const int c = cb * 16 + i;
                    d[i] = (m->dw_sw[c] * jb->sx[c]) * (float)acc[f * 16 + i] + m->dw_bf[c];
                }
            }
        }
    }
}

static void cnx_dw_part(void *ctx, int part, int parts)
{
    const cnx_job_t *jb = ctx;
    if (jb->x8) {
        cnx_dw_part_q8(jb, part, parts);
        return;
    }
    const pieasr_model_t *m = jb->m;
    const int F = 19, C = 128, T3 = 67;
    int c0, c1;
    pq_par_range(C, part, parts, 1, &c0, &c1);
    for (int c = c0; c < c1; c++) {
        const float *wk = m->dw_wf + c * 49;
        const float bias = pq_h2f(m->dw_b, c);
        for (int tt = 0; tt < jb->nt; tt++) {
            const float *rows[7];
            for (int ky = 0; ky < 7; ky++) {
                const int ti = jb->t + tt + ky;
                rows[ky] = ti < 3 ? m->left_pad + ((size_t)c * 3 + ti) * F : jb->h3 + ((size_t)c * T3 + ti - 3) * F;
            }
            // tap-major: the 19 outputs are independent; each still sums its taps in (ky, kx) order
            float acc[19];
            for (int f = 0; f < F; f++) {
                acc[f] = 0.0f;
            }
            for (int ky = 0; ky < 7; ky++) {
                const float *r = rows[ky];
                for (int kx = 0; kx < 7; kx++) {
                    const float wv = wk[ky * 7 + kx];
                    const int f0 = kx < 3 ? 3 - kx : 0, f1 = kx > 3 ? F + 3 - kx : F;   // 0 <= f + kx - 3 < F
                    for (int f = f0; f < f1; f++) {
                        acc[f] += wv * r[f + kx - 3];
                    }
                }
            }
            for (int f = 0; f < F; f++) {
                jb->d[(size_t)(tt * F + f) * C + c] = acc[f] + bias;
            }
        }
    }
}

void pq_front_end_q8(pieasr_model_t *m, const float *x, float *y)
{
    size_t mark = m->ws_used;
    int H, W, H2, W2;
    PQ_PROF_START();
    const int T3 = 67, F = 19, C = 128;
    float *h3 = pq_ws_alloc(m, (size_t)C * T3 * F);
    size_t mark2 = m->ws_used;   // h2 and the im2col blocks are freed once h3 is done
    // conv.0 (1 -> 8, 3x3, pad freq 1) in float, without border tests: the input padded with zero columns
    H = PIEASR_CHUNK_IN - 2;
    W = PIEASR_MEL;
    // h1 [8, 139, 80] in h3's place: conv.4 reads it, conv.7 writes h3 from h2 only
    _Static_assert(8 * 139 * 80 <= 128 * 67 * 19, "h1 fits in h3");
    float *h1 = h3;
    {
        float *xp = pq_ws_alloc(m, (size_t)PIEASR_CHUNK_IN * (PIEASR_MEL + 2));
        for (int r = 0; r < PIEASR_CHUNK_IN; r++) {
            xp[r * (PIEASR_MEL + 2)] = 0.0f;
            memcpy(xp + r * (PIEASR_MEL + 2) + 1, x + r * PIEASR_MEL, sizeof(float) * PIEASR_MEL);
            xp[r * (PIEASR_MEL + 2) + PIEASR_MEL + 1] = 0.0f;
        }
        conv0_job_t j = { .m = m, .xp = xp, .y = h1 };
        pq_par_run(conv0_part, &j);
        m->ws_used = mark2;
    }
    PQ_TRACE("embed.c0", h1, 8 * H * W);
    // conv.4 (8 -> 32, 3x3, stride 2) as im2col blocks + dense int8: col[p][ci*9 + ky*3 + kx]
    H2 = (H - 3) / 2 + 1;
    W2 = (W - 3) / 2 + 1;
    float *h2 = pq_ws_alloc(m, 32 * 69 * 39);
    {
        size_t mark4 = m->ws_used;   // conv.4's blocks freed before conv.7's
        const int P2 = H2 * W2, K4 = 8 * 9;
        float *col = pq_ws_alloc(m, (size_t)64 * K4), *yb = pq_ws_alloc(m, (size_t)64 * 32);
        for (int p0 = 0; p0 < P2; p0 += 64) {
            const int n = P2 - p0 < 64 ? P2 - p0 : 64;
            for (int i = 0; i < n; i++) {
                const int oy = (p0 + i) / W2, ox = (p0 + i) % W2;
                float *c = col + (size_t)i * K4;
                for (int ci = 0; ci < 8; ci++) {
                    for (int ky = 0; ky < 3; ky++) {
                        const float *r = h1 + ((size_t)ci * H + 2 * oy + ky) * W + 2 * ox;
                        c[ci * 9 + ky * 3 + 0] = r[0];
                        c[ci * 9 + ky * 3 + 1] = r[1];
                        c[ci * 9 + ky * 3 + 2] = r[2];
                    }
                }
            }
            pq_dense_apply(&m->conv4_q, col, n, K4, yb, 32, PQ_DENSE_POST_SWOOSH_R);
            for (int i = 0; i < n; i++) {
                for (int co = 0; co < 32; co++) {
                    h2[(size_t)co * P2 + p0 + i] = yb[(size_t)i * 32 + co];
                }
            }
        }
        PQ_TRACE("embed.c4", h2, 32 * P2);
        m->ws_used = mark4;
    }
    // conv.7 (32 -> 128, 3x3, stride (1, 2)) as im2col blocks of 64 positions: col[p][ci*9 + ky*3 + kx]
    const int P = T3 * F, KC = 32 * 9;
    float *col = pq_ws_alloc(m, (size_t)64 * KC), *yb = pq_ws_alloc(m, (size_t)64 * C);
    for (int p0 = 0; p0 < P; p0 += 64) {
        const int n = P - p0 < 64 ? P - p0 : 64;
        for (int i = 0; i < n; i++) {
            const int oy = (p0 + i) / F, ox = (p0 + i) % F;
            float *c = col + (size_t)i * KC;
            for (int ci = 0; ci < 32; ci++) {
                for (int ky = 0; ky < 3; ky++) {
                    const float *r = h2 + ((size_t)ci * H2 + oy + ky) * W2 + 2 * ox;
                    c[ci * 9 + ky * 3 + 0] = r[0];
                    c[ci * 9 + ky * 3 + 1] = r[1];
                    c[ci * 9 + ky * 3 + 2] = r[2];
                }
            }
        }
        pq_dense_apply(&m->conv7_q, col, n, KC, yb, C, PQ_DENSE_POST_SWOOSH_R);
        for (int i = 0; i < n; i++) {
            for (int co = 0; co < C; co++) {
                h3[(size_t)co * T3 * F + p0 + i] = yb[(size_t)i * C + co];
            }
        }
    }
    m->ws_used = mark2;
    PQ_PROF_END(front_conv);
    PQ_TRACE("embed.conv", h3, C * T3 * F);
    prof_t0 = pieasr_clock ? pieasr_clock() : 0;
    const int T = PQ_EMBED_T, TB = 3;
    // with the integer embed_out (its input scale is static, so its rows are independent) e holds TB frames at a time
    // and each block goes through the product as soon as it is done: less workspace
    const int eblk = pieasr_opt_int && m->embed_out.qi.n16;
    float *e = pq_ws_alloc(m, (size_t)(eblk ? TB : T) * C * F);
    size_t mark3 = m->ws_used;
    // per output frame t: depthwise 7x7 -> d [19 positions][128]; pointwise 128 -> 384 (swoosh_l) -> 128 (dense
    // int8); + bypass h3[:, t]; written into e [64, 128*19] (column c*19 + f)
    // 3 frames (57 positions) at a time
    float *d = pq_ws_alloc(m, (size_t)TB * F * C), *hid = pq_ws_alloc(m, (size_t)TB * F * 384);
    float *o = pq_ws_alloc(m, (size_t)TB * F * C);
    int8_t *hid8 = (int8_t *)(((uintptr_t)hid + 15) & ~(uintptr_t)15);   // (pieasr_opt_int: hid as int8 rows of 384)
    // the int8 ConvNeXt input [70][25][128] (zero frequency padding) and its per-channel scales
    int8_t *x8 = (int8_t *)pq_ws_alloc(m, (70 * 25 * 128 + 16) / 4 + 4);
    x8 = (int8_t *)(((uintptr_t)x8 + 15) & ~(uintptr_t)15);
    float *sx8 = pq_ws_alloc(m, 128);
    memset(x8, 0, 70 * 25 * 128);
    cnx_q_job_t qjob = { .m = m, .h3 = h3, .x8 = x8, .sx = sx8 };
    PQ_TIMED(cnx_dw, pq_par_run(cnx_quant_part, &qjob));
    for (int t0 = 0; t0 < T; t0 += TB) {
        const int nt = T - t0 < TB ? T - t0 : TB, np = nt * F;
        cnx_job_t job = { .m = m, .h3 = h3, .d = d, .t = t0, .nt = nt, .x8 = x8, .sx = sx8 };
        PQ_TIMED(cnx_dw, pq_par_run(cnx_dw_part, &job));
        if (pieasr_opt_int) {
            // the hidden SwooshL output as int8 with a static scale per channel (the calibrated 'fe.pw1'), read by
            // pw2 as is: the scales are folded into pw2's weights (a single scale for the tensor costs accuracy)
            PQ_TIMED(cnx_pw1, pq_dense_apply_ex(&m->pw1_q, d, NULL, 0.0f, np, C, NULL, hid8, m->pw1_is, 384,
                                                PQ_DENSE_POST_SWOOSH_L));
            PQ_TIMED(cnx_pw2, pq_dense_apply_ex(&m->pw2_q, NULL, hid8, 1.0f, np, 384, o, NULL, NULL, C,
                                                PQ_DENSE_POST_NONE));
        } else {
            PQ_TIMED(cnx_pw1, pq_dense_apply(&m->pw1_q, d, np, C, hid, 384, PQ_DENSE_POST_SWOOSH_L));
            PQ_TIMED(cnx_pw2, pq_dense_apply(&m->pw2_q, hid, np, 384, o, C, PQ_DENSE_POST_NONE));
        }
        for (int tt = 0; tt < nt; tt++) {
            const int t = t0 + tt, et = eblk ? tt : t;
            for (int f = 0; f < F; f++) {
                for (int c = 0; c < C; c++) {
                    e[(size_t)et * C * F + c * F + f] =
                        h3[((size_t)c * T3 + t) * F + f] + o[(size_t)(tt * F + f) * C + c];
                }
            }
        }
        if (eblk) {
            pq_linear(m, &m->embed_out, e, nt, y + (size_t)t0 * m->embed_out.w.out);
        }
    }
    // the new left pad: concat frames 64..66 = h3 frames 61..63 (after the old pad has been used)
    for (int c = 0; c < C; c++) {
        memcpy(m->left_pad + (size_t)c * 3 * F, h3 + ((size_t)c * T3 + T - 3) * F, sizeof(float) * 3 * F);
    }
    PQ_PROF_END(front_convnext);
    m->ws_used = mark3;
    if (!eblk) {
        pq_linear(m, &m->embed_out, e, T, y);
    }
    PQ_TRACE("embed.linear", y, T * 192);
    pq_bias_norm(y, T, 192, m->out_norm_bias, m->out_norm_scale);
    PQ_TRACE("embed.out", y, T * 192);
    m->ws_used = mark;
}
