// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// pieasr_init's preparation of the integer path (pieasr_opt_int): the products' int16 norms and scales, the int16
// residual's scales, the activation tables, the attention's and the conv modules' integer parameters.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernels/kernels.h"
#include "math/fastmath.h"
#include "math/tables/tables.h"
#include "model_internal.h"

// the functions of the int16 activation tables: fastmath.h's float versions (plain float arithmetic, a few 1e-7
// relative, far below the tables' int16 step), so that the host and the chip build the same tables (libm's float
// functions differ in the last bit between glibc and newlib; detmath.h's double versions are slow on the chip)
static float softplus_exact(float z)
{
    return pq_fm_softplus(z);
}

static float tanh_exact(float x)
{
    return pq_fm_tanh(x);
}

static float sigmoid_exact(float x)
{
    return pq_fm_sigmoid(x);
}

static float swoosh_l_exact(float x)
{
    const float z = x - 4.0f;
    return softplus_exact(z) - 0.08f * x - 0.035f;
}

static float swoosh_r_exact(float x)
{
    const float z = x - 1.0f;
    return softplus_exact(z) - 0.08f * x - 0.313261687f;
}

// the integer product of one matrix: int16 norm tiles over the float16 norms when in place (both 2 bytes per norm;
// the weights file aligns every tensor to 64 bytes), else in new memory
// s_res > 0: the product's output is added into an int16 residual of that scale (pqi_prepare_res)
static int prep_int_s(pieasr_model_t *m, pq_lin_t *l, void *(*alloc)(size_t), float s_res)
{
    if (l->ax <= 0.0f) {
        return 0;
    }
    const size_t n = (size_t)l->w.out * (l->w.in / l->w.G), mark = m->ws_used;
    int16_t *n16 = (int16_t *)l->w.norms;
    if (!pieasr_opt_inplace || ((uintptr_t)n16 & 15)) {
        uint8_t *p = alloc(sizeof(int16_t) * n + 16);
        if (!p) {
            return -1;
        }
        n16 = (int16_t *)(((uintptr_t)p + 15) & ~(uintptr_t)15);
    }
    uint16_t *tmp = (uint16_t *)pq_ws_alloc(m, (sizeof(uint16_t) * n + 3) / 4);
    int r = 0;
    if (s_res > 0.0f) {
        const int n = pqi_prepare_res(&l->qi, &l->w, l->ax, l->ay, s_res, n16, tmp);
        r = n < 1 ? -1 : 0;
    } else {
        pqi_prepare(&l->qi, &l->w, l->ax, l->ay, n16, tmp);
    }
    m->ws_used = mark;
    return r;
}

static int prep_int(pieasr_model_t *m, pq_lin_t *l, void *(*alloc)(size_t))
{
    return prep_int_s(m, l, alloc, 0.0f);
}

static float exp127(float x)
{
    return x <= 0.0f ? 127.0f * pq_fm_exp(x) : 127.0f;
}

// PIEASR_I16_AW: the integer parameters of the attention weights of layer (si, li) (after aw_in is prepared)
static int prep_aw_int(pieasr_model_t *m, int si, int li, void *(*alloc)(size_t))
{
    pq_stack_t *st = &m->stack[si];
    pq_layer_t *ly = &st->layer[li];
    const int H = st->H, L = st->L, S = st->S, npos = L + 2 * S - 1, wq = QD * H, W = 68 * H;
    char nm[40];
    snprintf(nm, sizeof(nm), "s%d.l%d.aw.q", si, li);
    const float aq = pq_act_max(nm);
    snprintf(nm, sizeof(nm), "s%d.l%d.aw.k", si, li);
    const float ak = pq_act_max(nm);
    snprintf(nm, sizeof(nm), "s%d.l%d.aw.pq", si, li);
    const float apq = pq_act_max(nm);
    snprintf(nm, sizeof(nm), "s%d.l%d.aw.sc", si, li);
    const float asc = pq_act_max(nm);
    if (aq <= 0.0f || ak <= 0.0f || apq <= 0.0f || asc <= 0.0f || H > 8 || ly->aw_in.w.out != W) {
        return -1;
    }
    const float sy = ly->aw_in.qi.sy, sq = aq / 127.0f, sk = ak / 127.0f, spq = apq / 127.0f;
    ly->aw_N = pq_alloc16(alloc, sizeof(int16_t) * W);
    ly->pos8 = alloc((size_t)npos * 4 * H);
    ly->aw_exp = alloc(sizeof(uint32_t) << (16 - PQ_ACT8_K));
    if (!ly->aw_N || !ly->pos8 || !ly->aw_exp) {
        return -1;
    }
    float nmax = 0.0f;
    for (int c = 0; c < W; c++) {
        nmax = pq_fmax(nmax, sy / (c < wq ? sq : c < 2 * wq ? sk : spq));
    }
    int sh = 0;
    while (sh < 30 && nmax * (float)(1 << (sh + 1)) <= 32767.0f) {
        sh++;
    }
    for (int c = 0; c < W; c++) {
        ly->aw_N[c] = (int16_t)pq_round(sy / (c < wq ? sq : c < 2 * wq ? sk : spq) * (float)(1 << sh));
    }
    ly->aw_sh = sh;
    // the position table int8, one scale per head
    float spos[8];
    for (int h = 0; h < H; h++) {
        float mx = 0.0f;
        for (int r = 0; r < npos; r++) {
            for (int d = 0; d < PD; d++) {
                mx = pq_fmax(mx, fabsf(ly->pos[(size_t)r * 4 * H + h * PD + d]));
            }
        }
        spos[h] = mx > 0.0f ? mx / 127.0f : 1.0f;
        for (int r = 0; r < npos; r++) {
            for (int d = 0; d < PD; d++) {
                const int v = pq_round(ly->pos[(size_t)r * 4 * H + h * PD + d] / spos[h]);
                ly->pos8[(size_t)r * 4 * H + h * PD + d] = (int8_t)(v > 127 ? 127 : v < -127 ? -127 : v);
            }
        }
    }
    // the scores at the step s_v
    const float s_v = 2.0f * asc / 32767.0f;
    float acc_max = 2.0f * asc / (sq * sk);
    acc_max = acc_max < 32.0f * 127.0f * 127.0f ? acc_max : 32.0f * 127.0f * 127.0f;
    int r1 = 0;
    while (r1 < 30 && acc_max / (float)(1 << r1) > 32767.0f) {
        r1++;
    }
    float gmax = (float)(1 << r1) * sq * sk / s_v;
    for (int h = 0; h < H; h++) {
        gmax = pq_fmax(gmax, 2.0f * spq * spos[h] / s_v);
    }
    int sh2 = 1;
    while (sh2 < 30 && gmax * (float)(1 << (sh2 + 1)) <= 32767.0f) {
        sh2++;
    }
    ly->aw_r1 = r1;
    ly->aw_sh2 = sh2;
    ly->aw_Mqk = pq_round((float)(1 << r1) * sq * sk / s_v * (float)(1 << sh2));
    for (int h = 0; h < H; h++) {
        ly->aw_Mpos[h] = pq_round(2.0f * spq * spos[h] / s_v * (float)(1 << sh2));
    }
    pq_tab_build_k(ly->aw_exp, exp127, s_v, 1.0f, PQ_ACT8_K);
    return 0;
}

// PIEASR_I16_CV: the integer parameters of conv module k of layer (si, li) (after its products are prepared)
static int prep_cv_int(pieasr_model_t *m, int si, int li, int k, void *(*alloc)(size_t))
{
    pq_stack_t *st = &m->stack[si];
    pq_layer_t *ly = &st->layer[li];
    const int D = st->D, S = st->S, K = st->K;
    char nm[40];
    snprintf(nm, sizeof(nm), "s%d.l%d.cv%d.dw", si, li, k + 1);
    const float vmax = 2.0f * pq_act_max(nm);   // headroom 2 on the combine's output (SwooshR's input)
    if (vmax <= 0.0f || !ly->cv_in[k].qi.n16 || ly->cv_in[k].qi.n != 8 || ly->cv_out[k].ax <= 0.0f || !ly->cv_swc[k]) {
        return -1;
    }
    const float sy = ly->cv_in[k].qi.sy, s_v = vmax / 32767.0f;
    ly->cv_tabb[k] = alloc(sizeof(uint32_t) * PQ_TAB_N);
    ly->cv_tabr[k] = alloc(sizeof(uint32_t) * PQ_TAB_N);
    ly->cv_cc[k] = alloc(sizeof(float) * 4 * D);
    float *es;
    if (pieasr_opt_inplace) {
        // the float edge scales a temporary, made here from the weights' chunkwise_conv_scale (float16 [2][D][K],
        // never read again on this path); their int16 copy goes over it when it fits (S <= 2K: every stack but
        // stack 0), else into a block of its own, half the float one's size
        es = malloc(sizeof(float) * D * S);
        if (es) {
            pq_conv_edge_scales(ly->cv_scale[k], D, K, S, es);
        }
        const size_t need = sizeof(int16_t) * D * S, have = sizeof(uint16_t) * 2 * D * K;
        ly->cv_es16[k] = need <= have ? (int16_t *)ly->cv_scale[k] : pq_alloc16(alloc, need);
        if (need <= have) {
            ly->cv_scale[k] = NULL;
        }
    } else {
        es = malloc(sizeof(float) * D * S);   // the float edge scales, read once: es16 replaces them in place
        if (es) {
            memcpy(es, ly->cv_es[k], sizeof(float) * D * S);
        }
        ly->cv_es16[k] = (int16_t *)ly->cv_es[k];
    }
    ly->cv_es[k] = NULL;
    if (!ly->cv_tabb[k] || !ly->cv_tabr[k] || !ly->cv_cc[k] || !es || !ly->cv_es16[k]) {
        free(es);
        return -1;
    }
    pq_tab_build(ly->cv_tabb[k], sigmoid_exact, sy, 1.0f / 32768.0f);
    pq_tab_build(ly->cv_tabr[k], swoosh_r_exact, s_v, ly->cv_out[k].ax / 32767.0f);
    // int8 inputs at full scale (dynamic per channel): acc <= 127 * 127 * K, a = acc >> r fits int16
    int r = 0;
    while (r < 30 && 127.0f * 127.0f * (float)K / (float)(1 << r) > 32767.0f) {
        r++;
    }
    for (int d = 0; d < D; d++) {
        const float f = sy * (float)(1 << r) / s_v;
        ly->cv_cc[k][d] = ly->cv_swc[k][d] * f;
        ly->cv_cc[k][D + d] = ly->cv_swk[k][d] * f;
        ly->cv_cc[k][2 * D + d] = ly->cv_bcf[k][d] / s_v;
        ly->cv_cc[k][3 * D + d] = ly->cv_bkf[k][d] / s_v;
        for (int t = 0; t < S; t++) {
            const int e = pq_round(es[d * S + t] * 2048.0f);
            if (e > 32767 || e < -32768) {   // (Q11: |es| < 16; the edge scales lie in [-7, 11])
                free(es);
                return -2;
            }
            ly->cv_es16[k][((size_t)(d / 16) * S + t) * 16 + (d % 16)] = (int16_t)e;
        }
    }
    free(es);
    ly->cv_r[k] = r;
    ly->cv_sv[k] = s_v;
    return 0;
}

// the integer parameters of every layer (after the weights are looked up and the dense copies made)
int pq_prepare_int(pieasr_model_t *m, void *(*alloc)(size_t))
{
    if (prep_int(m, &m->embed_out, alloc) || prep_int(m, &m->enc_proj, alloc)) {
        return -8;
    }
    for (int si = 0; si < PQ_STACKS; si++) {
        for (int li = 0; li < m->stack[si].layers; li++) {
            pq_layer_t *ly = &m->stack[si].layer[li];
            if (pieasr_opt_i16 & PIEASR_I16_RES) {   // one int16 scale for x0..x7, mid, out; x8 at 2^res_j times it
                char nm[32];
                float A = 0.0f;
                static const char *pts[] = { "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "mid", "out" };
                for (int k = 0; k < 10; k++) {
                    snprintf(nm, sizeof(nm), "s%d.l%d.%s", si, li, pts[k]);
                    A = pq_fmax(A, pq_act_max(nm));
                }
                snprintf(nm, sizeof(nm), "s%d.l%d.x8", si, li);
                const float a8 = pq_act_max(nm);
                int j = 0;
                while (j < 6 && A * (float)(1 << j) < a8) {
                    j++;
                }
                if (A > 0.0f) {
                    ly->res_s = A / 32767.0f;
                    ly->res_j = j;
                    pq_lin_t *rd[] = { &ly->aw_in, &ly->ff_in[0], &ly->ff_in[1], &ly->ff_in[2], &ly->na_in,
                                       &ly->sa_in[0], &ly->sa_in[1], &ly->cv_in[0], &ly->cv_in[1] };
                    for (int k = 0; k < 9; k++) {   // readers of the residual: their input scale is its scale
                        rd[k]->ax = A;
                    }
                    pq_lin_t *wr[] = { &ly->ff_out[0], &ly->ff_out[1], &ly->ff_out[2], &ly->na_out, &ly->sa_out[0],
                                       &ly->sa_out[1], &ly->cv_out[0], &ly->cv_out[1] };
                    for (int k = 0; k < 8; k++) {   // writers: their output step is the residual's scale / 2^n
                        const float sr = ly->res_s * (k == 2 ? (float)(1 << j) : 1.0f);
                        const int jk = k == 2 ? j : 0;   // (pq_out_add16 on PIE: 1 <= n - j <= 14)
                        if (prep_int_s(m, wr[k], alloc, sr) || wr[k]->qi.n - jk < 1 || wr[k]->qi.n - jk > 14) {
                            printf("pieasr_init: s%d.l%d writer %d: no residual output shift\n", si, li, k);
                            return -10;
                        }
                    }
                }
            }
            pq_lin_t *ls[] = { &ly->aw_in, &ly->sa_in[0], &ly->sa_in[1], &ly->sa_out[0], &ly->sa_out[1],
                               &ly->ff_in[0], &ly->ff_in[1], &ly->ff_in[2], &ly->ff_out[0], &ly->ff_out[1],
                               &ly->ff_out[2], &ly->na_in, &ly->na_out, &ly->cv_in[0], &ly->cv_in[1],
                               &ly->cv_out[0], &ly->cv_out[1] };
            for (int k = 0; k < (int)(sizeof(ls) / sizeof(ls[0])); k++) {
                if (ls[k]->qi.n16) {   // (prepared above for the residual)
                    continue;
                }
                if (prep_int(m, ls[k], alloc)) {
                    return -8;
                }
            }
            if ((pieasr_opt_i16 & PIEASR_I16_NA) && ly->nl8 && ly->na_in.qi.n16 && ly->na_in.qi.n == 8) {
                const int hd = ly->na_in.w.out / 3;
                const float sy = ly->na_in.qi.sy;
                ly->na_tab = alloc(sizeof(uint32_t) * PQ_TAB_N);
                ly->na_N = pq_alloc16(alloc, sizeof(int16_t) * hd);
                if (!ly->na_tab || !ly->na_N || (hd & 15)) {
                    return -14;
                }
                pq_tab_build(ly->na_tab, tanh_exact, sy, 1.0f / 32768.0f);
                float nmax = 0.0f;
                for (int c = 0; c < hd; c++) {
                    nmax = pq_fmax(nmax, ly->nl_s[c] > 0.0f ? sy / ly->nl_s[c] : 0.0f);
                }
                int sh2 = 0;
                while (sh2 < 30 && nmax * (float)(1 << (sh2 + 1)) <= 32767.0f) {
                    sh2++;
                }
                for (int c = 0; c < hd; c++) {
                    ly->na_N[c] = (int16_t)(ly->nl_s[c] > 0.0f ? pq_round(sy / ly->nl_s[c] * (float)(1 << sh2)) : 0);
                }
                ly->na_sh2 = sh2;
            }
            if ((pieasr_opt_i16 & PIEASR_I16_AW) && ly->key8 && ly->aw_in.qi.n16 && ly->aw_in.qi.n == 8) {
                if (prep_aw_int(m, si, li, alloc)) {
                    printf("pieasr_init: s%d.l%d attention: no integer parameters\n", si, li);
                    return -16;
                }
            }
            for (int k = 0; k < 2 && (pieasr_opt_i16 & PIEASR_I16_SA); k++) {
                const pq_lin_t *l = &ly->sa_in[k];
                const int wv = l->w.out;
                if (!ly->val8[k] || !l->qi.n16 || l->qi.n != 8 || (wv & 15)) {
                    continue;
                }
                ly->sa_N[k] = pq_alloc16(alloc, sizeof(int16_t) * wv);
                if (!ly->sa_N[k]) {
                    return -15;
                }
                float nmax = 0.0f;
                for (int c = 0; c < wv; c++) {
                    nmax = pq_fmax(nmax, ly->val_s[k][c] > 0.0f ? l->qi.sy / ly->val_s[k][c] : 0.0f);
                }
                int sh = 0;
                while (sh < 30 && nmax * (float)(1 << (sh + 1)) <= 32767.0f) {
                    sh++;
                }
                for (int c = 0; c < wv; c++) {
                    ly->sa_N[k][c] =
                        (int16_t)(ly->val_s[k][c] > 0.0f ? pq_round(l->qi.sy / ly->val_s[k][c] * (float)(1 << sh)) : 0);
                }
                ly->sa_sh[k] = sh;
            }
            for (int k = 0; k < 2 && (pieasr_opt_i16 & PIEASR_I16_CV); k++) {
                if (prep_cv_int(m, si, li, k, alloc)) {
                    printf("pieasr_init: s%d.l%d conv %d: no integer parameters\n", si, li, k);
                    return -13;
                }
            }
            for (int k = 0; k < 3; k++) {   // (built always: pieasr_opt_i16 may be switched after init)
                if (!ly->ff_in[k].qi.n16 || !ly->ff_out[k].qi.n16) {
                    continue;
                }
                ly->ff_tab[k] = alloc(sizeof(uint32_t) * PQ_TAB_N);
                if (!ly->ff_tab[k]) {
                    return -8;
                }
                pq_tab_build(ly->ff_tab[k], swoosh_l_exact, ly->ff_in[k].qi.sy, ly->ff_out[k].ax / 32767.0f);
            }
        }
    }
    return 0;
}
