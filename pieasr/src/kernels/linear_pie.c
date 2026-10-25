// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The int8 2/4-bit product organized for the PIE kernels (decode, tile16), with the same arithmetic and float order
// as pq_linear_q8; the decode tables and the kernels' internal-RAM buffers.
#include <math.h>
#include <string.h>
#include "kernels/kernels.h"
#include "kernels/linear.h"
#include "runtime/parallel.h"

pq_pie_prof_t pq_pie_prof;

static uint32_t lut2[256] __attribute__((aligned(16)));
static uint32_t lut4[256] __attribute__((aligned(16)));

void pq_pie_init(void)
{
    for (int b = 0; b < 256; b++) {
        uint32_t v = 0;
        for (int r = 0; r < 4; r++) {
            v |= (uint32_t)(uint8_t)pq_lq2[(b >> (2 * r)) & 3] << (8 * r);
        }
        lut2[b] = v;
        lut4[b] = (uint32_t)(uint8_t)pq_lq4[b & 15] | (uint32_t)(uint8_t)pq_lq4[b >> 4] << 8;
    }
}

// scratch layout: xq [T][in] int8 (+32 slack, 16-aligned) | sx [T][ng] float | acc [T][16] int32 | wdec [G*16 + 32]
size_t pq_linear_pie_scratch(const pq_cqmat_t *w, int T)
{
    const int ng = w->in / w->G;
    return ((size_t)T * w->in + 32 + 15) / 16 * 16 + sizeof(float) * T * ng + sizeof(int32_t) * T * 16 +
           (size_t)w->G * 16 + 32 + 16;
}

// the kernels' working buffers, in internal RAM (T <= 64 frames)
#define XQ_INTERNAL (48 * 1024)
static int8_t xq_buf[XQ_INTERNAL] __attribute__((aligned(16)));
static float sx_buf[64 * 20];
// per core
static int32_t acc_buf[PQ_PAR_PARTS][64 * 16] __attribute__((aligned(16)));
static float yacc_buf[PQ_PAR_PARTS][64 * 16];
static int8_t wdec_buf[PQ_PAR_PARTS][128 * 16 + 32] __attribute__((aligned(16)));

const uint32_t *pq_lut(int bits)
{
    return bits == 2 ? lut2 : lut4;
}

void pq_get_bufs(pq_bufs_t *b)
{
    b->xq = xq_buf;
    b->xq_cap = sizeof(xq_buf);
    b->sx = sx_buf;
    b->sx_cap = sizeof(sx_buf);
    for (int p = 0; p < 2; p++) {
        b->acc[p] = acc_buf[p];
        b->wdec[p] = wdec_buf[p];
    }
}

typedef struct {
    const pq_cqmat_t *w;
    const uint16_t *bias;
    const float *x;
    float *y;
    int T, ldx, ldy;
    int8_t *xq;
    float *sx;
} cq_job_t;

// phase 1: rotate and quantize the input frames of this part (as pq_linear_q8)
static void cq_quant_part(void *ctx, int part, int parts)
{
    const cq_job_t *j = ctx;
    const int G = j->w->G, in = j->w->in, ng = in / G;
    int t0, t1;
    pq_par_range(j->T, part, parts, 1, &t0, &t1);
    float r[128];
    for (int t = t0; t < t1; t++) {
        for (int g = 0; g < ng; g++) {
            memcpy(r, j->x + (size_t)t * j->ldx + g * G, sizeof(float) * G);
            pq_fwht_f32(r, G);
            // max |r| with 4 independent partial maxima (the same value as one running max; no long fmax chain)
            float m0 = 0.0f, m1 = 0.0f, m2 = 0.0f, m3 = 0.0f;
            for (int k = 0; k < G; k += 4) {
                const float a0 = fabsf(r[k]), a1 = fabsf(r[k + 1]), a2 = fabsf(r[k + 2]), a3 = fabsf(r[k + 3]);
                m0 = pq_fmax(m0, a0);
                m1 = pq_fmax(m1, a1);
                m2 = pq_fmax(m2, a2);
                m3 = pq_fmax(m3, a3);
            }
            m0 = pq_fmax(m0, m1);
            m2 = pq_fmax(m2, m3);
            const float amax = pq_fmax(m0, m2);
            float inv = amax > 0.0f ? 127.0f / amax : 0.0f;
            j->sx[t * ng + g] = amax / 127.0f;
            int8_t *q = j->xq + (size_t)t * in + g * G;
#pragma GCC unroll 8
            for (int k = 0; k < G; k++) {
                q[k] = (int8_t)pq_round(r[k] * inv);
            }
        }
    }
}

// phase 2: the tiles of 16 rows of this part
static void cq_tiles_part(void *ctx, int part, int parts)
{
    const cq_job_t *j = ctx;
    const pq_cqmat_t *w = j->w;
    const int G = w->G, in = w->in, ng = in / G, tile = G * 2 * w->bits, T = j->T;
    const float lsc = w->bits == 2 ? pq_lsc2 : pq_lsc4, invG = 1.0f / (float)G;
    int32_t *acc = acc_buf[part];
    float *yacc = yacc_buf[part];
    int8_t *wdec = wdec_buf[part];
    int rb0, rb1;
    pq_par_range(w->out / 16, part, parts, 1, &rb0, &rb1);
    for (int rb = rb0; rb < rb1; rb++) {
        memset(yacc, 0, sizeof(float) * T * 16);
        for (int g = 0; g < ng; g++) {
            const uint8_t *codes = w->codes + (size_t)(rb * ng + g) * tile;
            uint32_t c0 = pq_cycles();
            if (w->bits == 2) {
                pq_decode2(codes, tile, lut2, wdec);
            } else {
                pq_decode4(codes, tile, lut4, wdec);
            }
            uint32_t c1 = pq_cycles();
            pq_tile16(wdec, j->xq + g * G, in, T, G, acc);
            uint32_t c2 = pq_cycles();
            float s[16];
#pragma GCC unroll 16
            for (int i = 0; i < 16; i++) {   // (* invG exact: G is a power of 2)
                s[i] = pq_f16_to_f32(w->norms[(size_t)(rb * 16 + i) * ng + g]) * invG * lsc;
            }
            for (int t = 0; t < T; t++) {
                float *yt = yacc + t * 16;
                const float sxt = j->sx[t * ng + g];
                const int32_t *a = acc + t * 16;
#pragma GCC unroll 16
                for (int i = 0; i < 16; i++) {
                    yt[i] += (s[i] * sxt) * (float)a[i];
                }
            }
            if (part == 0) {   // core 0's share only (no shared counters between the cores)
                pq_pie_prof.decode += c1 - c0;
                pq_pie_prof.tile += c2 - c1;
                pq_pie_prof.post += pq_cycles() - c2;
            }
        }
        float b[16];
        for (int i = 0; i < 16; i++) {
            b[i] = j->bias ? pq_f16_to_f32(j->bias[rb * 16 + i]) : 0.0f;
        }
        for (int t = 0; t < T; t++) {
            float *yt = j->y + (size_t)t * j->ldy + rb * 16;
            for (int i = 0; i < 16; i++) {
                yt[i] = yacc[t * 16 + i] + b[i];   // + 0.0f without a bias, as pq_linear_q8
            }
        }
    }
}

void pq_linear_pie(const pq_cqmat_t *w, const uint16_t *bias, const float *x, int T, int ldx, float *y, int ldy,
                   void *scratch)
{
    const int in = w->in, ng = in / w->G;
    uint8_t *p = (uint8_t *)(((uintptr_t)scratch + 15) & ~(uintptr_t)15);
    cq_job_t j = { .w = w, .bias = bias, .x = x, .y = y, .T = T, .ldx = ldx, .ldy = ldy };
    // the quantized inputs in internal RAM when they fit, else in the (PSRAM) scratch
    j.xq = (size_t)T * in + 32 <= XQ_INTERNAL ? xq_buf : (int8_t *)p;
    p += ((size_t)T * in + 32 + 15) / 16 * 16;
    j.sx = T * ng <= 64 * 20 ? sx_buf : (float *)p;
    pq_pie_prof.calls++;
    pq_pie_prof.macs += (uint64_t)T * in * w->out;
    uint32_t c0 = pq_cycles();
    pq_par_run(cq_quant_part, &j);
    pq_pie_prof.quant += pq_cycles() - c0;
    pq_par_run(cq_tiles_part, &j);
}
