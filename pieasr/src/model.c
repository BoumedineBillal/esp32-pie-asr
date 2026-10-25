// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The model: pieasr_init (the weights looked up, the state laid out, the preparation), reset, the options.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "encoder/encoder_internal.h"
#include "math/fastmath.h"
#include "math/tables/tables.h"
#include "model_internal.h"
#include "runtime/parallel.h"

// the six encoder stacks: layers, width, downsampling, heads, feed-forward width, conv kernel
static const int ST_LAYERS[PQ_STACKS] = { 2, 2, 3, 4, 3, 2 };
static const int ST_DIM[PQ_STACKS] = { 192, 256, 384, 512, 384, 256 };
static const int ST_DS[PQ_STACKS] = { 1, 2, 4, 8, 4, 2 };
static const int ST_HEADS[PQ_STACKS] = { 4, 4, 4, 8, 4, 4 };
static const int ST_FF[PQ_STACKS] = { 512, 768, 1024, 1536, 1024, 768 };
static const int ST_K[PQ_STACKS] = { 31, 31, 15, 15, 15, 31 };

pieasr_trace_fn pieasr_trace;
int pieasr_opt_q8;
pieasr_prof_t pieasr_prof;
size_t pieasr_opt_ws_floats = 512 * 1024;
int pieasr_opt_int;
int pieasr_opt_i16;
uint64_t (*pieasr_clock)(void);
int pieasr_opt_inplace;

float *pq_ws_alloc(pieasr_model_t *m, size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (m->ws_used + n > m->ws_floats) {
        fprintf(stderr, "pieasr: workspace too small (%u + %u > %u floats)\n", (unsigned)m->ws_used, (unsigned)n,
                (unsigned)m->ws_floats);
        abort();
    }
    float *p = m->ws + m->ws_used;
    m->ws_used += n;
    if (m->ws_used > m->ws_peak) {
        m->ws_peak = m->ws_used;
#ifdef PIEASR_WS_TRACE   // which allocation raises the peak (addr2line names the caller)
        fprintf(stderr, "ws peak %u floats, +%u at %p\n", (unsigned)m->ws_used, (unsigned)n,
                __builtin_return_address(0));
#endif
    }
    return p;
}

static const uint16_t *F16(pieasr_model_t *m, const char *name)
{
    return pq_weights_f16(&m->weights, name, NULL);
}

static pq_lin_t LIN(pieasr_model_t *m, const char *name, int with_bias)
{
    pq_lin_t l = { .w = pq_weights_cq(&m->weights, name), .b = NULL };
    for (int i = 0; i < PQ_NSCALES; i++) {
        if (!strcmp(pq_scales[i].name, name)) {
            l.ax = pq_scales[i].ax;
            l.ay = pq_scales[i].ay;
        }
    }
    if (with_bias) {
        char b[128];
        snprintf(b, sizeof(b), "%s.bias", name);
        l.b = F16(m, b);
    }
    return l;
}

size_t pieasr_model_size(void)
{
    return sizeof(pieasr_model_t);
}

int pieasr_init(pieasr_model_t *m, const void *weights, size_t size, void *(*alloc)(size_t))
{
    pq_par_init();
    pq_pie_init();
    memset(m, 0, sizeof(*m));
    if (pq_weights_open(&m->weights, weights, size)) {
        return -1;
    }
    if (pq_fm_tables_init(alloc)) {
        return -5;
    }
    for (int p = 0; p < PQ_PAR_PARTS; p++) {   // the int8 attn@V buffers (16-byte aligned for the PIE loads)
        uint8_t *b = alloc(64 * 320 + 320 * 16 + 32 + 64 * 4 + 64 * 16 * 4 + 64);
        if (!b) {
            return -6;
        }
        b = (uint8_t *)(((uintptr_t)b + 15) & ~(uintptr_t)15);
        pq_avq[p].xq = (int8_t *)b;
        pq_avq[p].vt = (int8_t *)(b + 64 * 320);
        pq_avq[p].acc = (int32_t *)(b + 64 * 320 + 320 * 16 + 32);
        pq_avq[p].xs = (float *)(b + 64 * 320 + 320 * 16 + 32 + 64 * 16 * 4);
        pq_avq[p].v16 = alloc(sizeof(int16_t) * 64 * 320);
        if (!pq_avq[p].v16) {
            return -6;
        }
        // the int8 depthwise buffers (lp <= 15, S <= 64)
        uint8_t *c = alloc(1264 + 1504 + 2 * 4096 + 5056 + 64);
        if (!c) {
            return -6;
        }
        c = (uint8_t *)(((uintptr_t)c + 15) & ~(uintptr_t)15);
        pq_dwq[p].xc = (int8_t *)c;
        pq_dwq[p].gz = (int8_t *)(c + 1264);
        pq_dwq[p].accc = (int32_t *)(c + 1264 + 1504 + 16);
        pq_dwq[p].acck = (int32_t *)(c + 1264 + 1504 + 16 + 4096);
        pq_dwq[p].gb = (float *)(c + 1264 + 1504 + 16 + 2 * 4096);
        pq_dwq[p].g16 = pq_alloc16(alloc, sizeof(int16_t) * (15 + 64) * 16);
        pq_dwq[p].comb = pq_alloc16(alloc, sizeof(pq_comb_t));
        if (!pq_dwq[p].g16 || !pq_dwq[p].comb) {
            return -6;
        }
    }
    for (int k = 0; k < 32; k++) {
        pq_dw_off[k] = 16 * k;
    }
    char n[128];
    const char *E = "encoder_embed.";
#define NM(...) (snprintf(n, sizeof(n), __VA_ARGS__), n)
    m->conv0_w = F16(m, NM("%sconv.0.weight", E));
    m->conv0_b = F16(m, NM("%sconv.0.bias", E));
    m->conv4_w = F16(m, NM("%sconv.4.weight", E));
    m->conv4_b = F16(m, NM("%sconv.4.bias", E));
    m->conv7_w = F16(m, NM("%sconv.7.weight", E));
    m->conv7_b = F16(m, NM("%sconv.7.bias", E));
    m->dw_w = F16(m, NM("%sconvnext.depthwise_conv.weight", E));
    m->dw_b = F16(m, NM("%sconvnext.depthwise_conv.bias", E));
    m->pw1_w = F16(m, NM("%sconvnext.pointwise_conv1.weight", E));
    m->pw1_b = F16(m, NM("%sconvnext.pointwise_conv1.bias", E));
    m->pw2_w = F16(m, NM("%sconvnext.pointwise_conv2.weight", E));
    m->pw2_b = F16(m, NM("%sconvnext.pointwise_conv2.bias", E));
    m->out_norm_bias = F16(m, NM("%sout_norm.bias", E));
    m->out_norm_scale = pq_fm_exp(pq_f16_to_f32(F16(m, NM("%sout_norm.log_scale", E))[0]));
    m->embed_out = LIN(m, "encoder_embed.out", 1);
    // state sizes, then the state buffer
    // (bytes; every piece rounded to 16. pieasr_opt_int: the attention caches int8 + key scales, else float)
#define R16(b) (((size_t)(b) + 15) & ~(size_t)15)
    size_t total = R16(sizeof(float) * 128 * 3 * 19), pos_total = 0;
    for (int si = 0; si < PQ_STACKS; si++) {
        int D = ST_DIM[si], H = ST_HEADS[si], L = PQ_LEFT / ST_DS[si], S = PQ_EMBED_T / ST_DS[si], lp = ST_K[si] / 2;
        size_t per = 2 * R16(sizeof(float) * D * lp);
        if (pieasr_opt_int) {
            per += R16((size_t)L * QD * H) + R16(sizeof(float) * L * H) + R16((size_t)L * 3 * D / 4) +
                   2 * R16((size_t)L * VD * H);
        } else {
            per += R16(sizeof(float) * L * QD * H) + R16(sizeof(float) * L * 3 * D / 4) +
                   2 * R16(sizeof(float) * L * VD * H);
        }
        total += (size_t)ST_LAYERS[si] * per;
        pos_total += (size_t)ST_LAYERS[si] * (L + 2 * S - 1) * 4 * H;
    }
    m->state_bytes = total;
    m->state = alloc(total);
    // with PIEASR_I16_AW the float position table is read only by the preparation (its int8 copy pos8): a temporary,
    // freed at the end of init when every layer has pos8
    const int pos_tmp = pieasr_opt_int && (pieasr_opt_i16 & PIEASR_I16_AW);
    float *pos = pos_tmp ? malloc(sizeof(float) * pos_total) : alloc(sizeof(float) * pos_total);
    float *const pos_base = pos;
    m->ws_floats = pieasr_opt_ws_floats;   // 2 MB by default (the float front end needs 507,008 floats)
    m->ws = alloc(sizeof(float) * m->ws_floats);
    if (!m->state || !pos || !m->ws) {
        return -2;
    }
    uint8_t *sp = m->state;
    m->left_pad = (float *)sp;
    sp += R16(sizeof(float) * 128 * 3 * 19);
    for (int si = 0; si < PQ_STACKS; si++) {
        pq_stack_t *st = &m->stack[si];
        st->layers = ST_LAYERS[si];
        st->D = ST_DIM[si];
        st->ds = ST_DS[si];
        st->H = ST_HEADS[si];
        st->ff = ST_FF[si];
        st->K = ST_K[si];
        st->L = PQ_LEFT / st->ds;
        st->S = PQ_EMBED_T / st->ds;
        if (st->ds > 1) {
            const uint16_t *b = F16(m, NM("encoder.encoders.%d.downsample.bias", si));
            float mx = -INFINITY, s = 0.0f;
            for (int i = 0; i < st->ds; i++) {
                st->ds_w[i] = pq_f16_to_f32(b[i]);
                mx = st->ds_w[i] > mx ? st->ds_w[i] : mx;
            }
            for (int i = 0; i < st->ds; i++) {
                st->ds_w[i] = pq_fm_exp(st->ds_w[i] - mx);
                s += st->ds_w[i];
            }
            for (int i = 0; i < st->ds; i++) {
                st->ds_w[i] /= s;
            }
            st->out_combiner = F16(m, NM("encoder.encoders.%d.out_combiner.bypass_scale", si));
        }
        char pre[64];
        if (si == 0) {
            snprintf(pre, sizeof(pre), "encoder.encoders.0.layers");
        } else {
            snprintf(pre, sizeof(pre), "encoder.encoders.%d.encoder.layers", si);
        }
        const int H = st->H, L = st->L, S = st->S, D = st->D, lp = st->K / 2, npos = L + 2 * S - 1;
        float *pe = malloc(sizeof(float) * npos * POS_DIM);
        pq_rel_pos_table(L, S, pe);
        for (int li = 0; li < st->layers; li++) {
            pq_layer_t *ly = &st->layer[li];
#define LP(fmt, ...) NM("%s.%d." fmt, pre, li, ##__VA_ARGS__)
            ly->aw_in = LIN(m, LP("self_attn_weights.in_proj"), 1);
            pq_lin_t aw_pos = LIN(m, LP("self_attn_weights.linear_pos"), 0);
            for (int k = 0; k < 2; k++) {
                ly->sa_in[k] = LIN(m, LP("self_attn%d.in_proj", k + 1), 1);
                ly->sa_out[k] = LIN(m, LP("self_attn%d.out_proj", k + 1), 1);
                ly->cv_in[k] = LIN(m, LP("conv_module%d.in_proj", k + 1), 1);
                ly->cv_out[k] = LIN(m, LP("conv_module%d.out_proj", k + 1), 1);
                ly->cv_causal_w[k] = F16(m, LP("conv_module%d.depthwise_conv.causal_conv.weight", k + 1));
                ly->cv_causal_b[k] = F16(m, LP("conv_module%d.depthwise_conv.causal_conv.bias", k + 1));
                ly->cv_chunk_w[k] = F16(m, LP("conv_module%d.depthwise_conv.chunkwise_conv.weight", k + 1));
                ly->cv_chunk_b[k] = F16(m, LP("conv_module%d.depthwise_conv.chunkwise_conv.bias", k + 1));
                ly->cv_scale[k] = F16(m, LP("conv_module%d.depthwise_conv.chunkwise_conv_scale", k + 1));
                if (pq_prep_conv_taps(ly, k, st->D, st->K, alloc)) {
                    return -7;
                }
                // the chunk edge scale 1 + (left + right) [D][S] (PIEASR_I16_CV: then its int16 copy; in place with
                // it, the preparation makes the floats itself, one layer at a time, so they are never all held at once)
                if (!(pieasr_opt_inplace && pieasr_opt_int && (pieasr_opt_i16 & PIEASR_I16_CV))) {
                    float *e = pq_alloc16(alloc, sizeof(float) * st->D * st->S);
                    if (!e) {
                        return -7;
                    }
                    pq_conv_edge_scales(ly->cv_scale[k], st->D, st->K, st->S, e);
                    ly->cv_es[k] = e;
                }
            }
            for (int k = 0; k < 3; k++) {
                ly->ff_in[k] = LIN(m, LP("feed_forward%d.in_proj", k + 1), 1);
                ly->ff_out[k] = LIN(m, LP("feed_forward%d.out_proj", k + 1), 1);
            }
            ly->na_in = LIN(m, LP("nonlin_attention.in_proj"), 1);
            ly->na_out = LIN(m, LP("nonlin_attention.out_proj"), 1);
            ly->norm_bias = F16(m, LP("norm.bias"));
            ly->norm_scale = pq_fm_exp(pq_f16_to_f32(F16(m, LP("norm.log_scale"))[0]));
            ly->bypass_mid = F16(m, LP("bypass_mid.bypass_scale"));
            ly->bypass = F16(m, LP("bypass.bypass_scale"));
#undef LP
            // the positional projection is a constant per layer
            ly->pos = pos;
            pos += (size_t)npos * 4 * H;
            float *scratch = malloc(sizeof(float) * npos * POS_DIM);
            pq_linear_ref(&aw_pos.w, NULL, pe, npos, POS_DIM, ly->pos, 4 * H, scratch);
            free(scratch);
            if (pieasr_opt_int) {
                ly->key8 = (int8_t *)sp;
                sp += R16((size_t)L * QD * H);
                ly->ks = (float *)sp;
                sp += R16(sizeof(float) * L * H);
                ly->nl8 = (int8_t *)sp;
                sp += R16((size_t)L * 3 * D / 4);
                for (int k = 0; k < 2; k++) {
                    ly->val8[k] = (int8_t *)sp;
                    sp += R16((size_t)L * VD * H);
                }
                // the values' static scales per channel (calibrated, pq_actch)
                const int hd = 3 * D / 4, wv = VD * H;
                float *sc = alloc(sizeof(float) * 2 * (hd + 2 * wv));
                if (!sc) {
                    return -2;
                }
                const char *nm[3] = { "na.vt", "sa1.v", "sa2.v" };
                float **dst[3][2] = { { &ly->nl_s, &ly->nl_is }, { &ly->val_s[0], &ly->val_is[0] },
                                      { &ly->val_s[1], &ly->val_is[1] } };
                for (int v = 0; v < 3; v++) {
                    char key[32];
                    snprintf(key, sizeof(key), "s%d.l%d.%s", si, li, nm[v]);
                    const int nc = v == 0 ? hd : wv;
                    const float *amax = NULL;
                    for (int i = 0; i < PQ_NACTCH; i++) {
                        if (!strcmp(pq_actch[i].name, key) && pq_actch[i].n == nc) {
                            amax = pq_actch[i].amax;
                        }
                    }
                    if (!amax) {
                        return -9;
                    }
                    *dst[v][0] = sc;
                    *dst[v][1] = sc + nc;
                    for (int c = 0; c < nc; c++) {
                        sc[c] = amax[c] / 127.0f;
                        sc[nc + c] = amax[c] > 0.0f ? 127.0f / amax[c] : 0.0f;
                    }
                    sc += 2 * nc;
                }
            } else {
                ly->key = (float *)sp;
                sp += R16(sizeof(float) * L * QD * H);
                ly->nonlin = (float *)sp;
                sp += R16(sizeof(float) * L * 3 * D / 4);
                for (int k = 0; k < 2; k++) {
                    ly->val[k] = (float *)sp;
                    sp += R16(sizeof(float) * L * VD * H);
                }
            }
            for (int k = 0; k < 2; k++) {
                ly->conv[k] = (float *)sp;
                sp += R16(sizeof(float) * D * lp);
            }
        }
        free(pe);
    }
    {
        const uint16_t *b = F16(m, "encoder.downsample_output.bias");
        float a = pq_f16_to_f32(b[0]), c = pq_f16_to_f32(b[1]), mx = a > c ? a : c;
        float ea = pq_fm_exp(a - mx), ec = pq_fm_exp(c - mx);
        m->out_ds_w[0] = ea / (ea + ec);
        m->out_ds_w[1] = ec / (ea + ec);
    }
    m->enc_proj = LIN(m, "joiner.encoder_proj", 1);
    m->emb = pq_weights_q8(&m->weights, "decoder.embedding.weight");
    m->dec_proj = pq_weights_q8(&m->weights, "joiner.decoder_proj.weight");
    m->out_lin = pq_weights_q8(&m->weights, "joiner.output_linear.weight");
    m->dec_conv_w = F16(m, "decoder.conv.weight");
    m->dec_proj_b = F16(m, "joiner.decoder_proj.bias");
    m->out_lin_b = F16(m, "joiner.output_linear.bias");
    const pq_weights_entry_t *te = pq_weights_find(&m->weights, "tokens");
    if (te) {
        const char *s = (const char *)m->weights.base + te->off_a;
        for (int i = 0; i < (int)te->shape[0] && i < PIEASR_VOCAB + 2; i++) {
            m->tokens[i] = s;
            s += strlen(s) + 1;
        }
    }
#undef NM
    {
        const int r = pq_prepare_dense(m, alloc);
        if (r) {
            return r;
        }
    }
    if (pieasr_opt_int) {
        const int r = pq_prepare_int(m, alloc);
        if (r) {
            return r;
        }
    }
    if (pos_tmp) {   // the float position table, if no layer reads it any more
        int all = 1;
        for (int si = 0; si < PQ_STACKS; si++) {
            for (int li = 0; li < m->stack[si].layers; li++) {
                all &= m->stack[si].layer[li].pos8 != NULL;
            }
        }
        if (all) {
            for (int si = 0; si < PQ_STACKS; si++) {
                for (int li = 0; li < m->stack[si].layers; li++) {
                    m->stack[si].layer[li].pos = NULL;
                }
            }
            free(pos_base);
        }
    }
    pieasr_reset(m);
    return 0;
}

void pieasr_reset(pieasr_model_t *m)
{
    memset(m->state, 0, m->state_bytes);
    m->processed = 0;
    m->ctx[0] = m->ctx[1] = 0;
    pq_update_decoder(m);
}

const char *const *pieasr_tokens(const pieasr_model_t *m)
{
    return m->tokens;
}

size_t pieasr_workspace_peak(const pieasr_model_t *m)
{
    return m->ws_peak;
}
