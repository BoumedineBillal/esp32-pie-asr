// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The feed-forward module: in_proj, SwooshL, out_proj.
#include "encoder/encoder_internal.h"
#include "math/fastmath.h"

// x[i] = f(x[i]), elements split between the cores
typedef struct {
    float *x;
    int n;
    float (*f)(float);
} map_job_t;

// swoosh_l in place (inlined), elements split between the cores
static void swoosh_l_part(void *ctx, int part, int parts)
{
    const map_job_t *jb = ctx;
    int a, b;
    pq_par_range(jb->n, part, parts, 16, &a, &b);
    float *x = jb->x;
    for (int i = a; i < b; i++) {
        x[i] = pq_fm_swoosh_l_tab(x[i]);
    }
}

void pq_feed_forward(pieasr_model_t *m, const pq_lin_t *in, const pq_lin_t *out, const uint32_t *tab, const float *x,
                     int S, float *y, int acc)
{
    size_t mark = m->ws_used;
    if (pieasr_opt_int && tab && (pieasr_opt_i16 & PIEASR_I16_FF)) {
        // the hidden int16: in_proj -> SwooshL table -> int16 at out_proj's input scale -> out_proj
        const int F = in->w.out;
        int16_t *h16 = (int16_t *)(((uintptr_t)pq_ws_alloc(m, ((size_t)S * F + 8) / 2 + 4) + 15) & ~(uintptr_t)15);
        PQ_PROF_START();
        size_t mk = m->ws_used;
        pqi_io_t a = { .T = S, .ldx = in->w.in, .y16 = h16, .tab = tab, .ldy = F,
                       .scratch = pq_ws_alloc(m, (pq_linear_int_scratch(&in->w, S) + 3) / 4) };
        pq_io_in(m, &a, x);
        pq_linear_int_io(&in->w, &in->qi, in->b, &a);
        m->ws_used = mk;
        pqi_io_t b = { .x16 = h16, .T = S, .ldx = F, .ldy = out->w.out,
                       .scratch = pq_ws_alloc(m, (pq_linear_int_scratch(&out->w, S) + 3) / 4) };
        pq_io_out(m, &b, y, acc ? PQI_POST_ADD : PQI_POST_NONE);
        pq_linear_int_io(&out->w, &out->qi, out->b, &b);
        PQ_PROF_END(linear);
        m->ws_used = mark;
        return;
    }
    float *h = pq_ws_alloc(m, (size_t)S * in->w.out);
    if (pieasr_opt_int) {   // SwooshL fused into in_proj's output loop, the residual add into out_proj's
        pq_linear_p(m, in, x, S, h, PQI_POST_SWOOSH_L);
        pq_linear_p(m, out, h, S, y, acc ? PQI_POST_ADD : PQI_POST_NONE);
        m->ws_used = mark;
        return;
    }
    pq_linear(m, in, x, S, h);
    map_job_t job = { .x = h, .n = S * in->w.out, .f = NULL };
    PQ_TIMED(ffsw, pq_par_run(swoosh_l_part, &job));
    pq_linear_p(m, out, h, S, y, acc ? PQI_POST_ADD : PQI_POST_NONE);
    m->ws_used = mark;
}
