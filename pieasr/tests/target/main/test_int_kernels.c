// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The integer kernels (PIE) against their C models on the chip: tile16_n, q8_shift, the Hadamards, the output and
// norm kernels, the conv module's kernels. Each prints "<kernel>: <n> wrong ..." and its cycles.
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "kernels/kernels.h"
#include "test_common.h"

// pq_tile16_n (the integer product's tile) against its C model: random and extreme values, every SAR 0..31,
// y32 near the int32 limits (vadd.s32 saturation); then its time per frame next to pq_tile16 (G 128, T 64)
static void test_tile16_n(void)
{
    const int T = 64, G = 128;
    int8_t *w = heap_caps_aligned_alloc(16, G * 16 + 32, MALLOC_CAP_INTERNAL);
    int8_t *x = heap_caps_aligned_alloc(16, T * G + 32, MALLOC_CAP_INTERNAL);
    int16_t *n16 = heap_caps_aligned_alloc(16, 32, MALLOC_CAP_INTERNAL);
    int32_t *y = heap_caps_aligned_alloc(16, T * 16 * 4, MALLOC_CAP_INTERNAL);
    int32_t *ref = malloc(T * 16 * 4);
    uint8_t sar[64];
    if (!w || !x || !n16 || !y || !ref) {
        report("tile16_n: err nomem");
        return;
    }
    int bad = 0, first = -1;
    for (int rep = 0; rep < 8; rep++) {
        for (int i = 0; i < G * 16; i++) {
            w[i] = (int8_t)(test_rnd() % 255 - 127);
        }
        for (int i = 0; i < T * G; i++) {
            x[i] = (int8_t)(test_rnd() % 255 - 127);
        }
        for (int i = 0; i < 16; i++) {
            n16[i] = (int16_t)(test_rnd() % 65535 - 32767);
        }
        if (rep == 0) {   // the largest sums, the largest norms
            memset(w, 127, G * 16);
            memset(x, 127, G);
            memset(x + G, -127, G);
            n16[0] = 32767;
            n16[1] = -32767;
        }
        for (int t = 0; t < T; t++) {
            sar[t] = (uint8_t)((t + rep) % 32);
            for (int i = 0; i < 16; i++) {
                int32_t v = (int32_t)(test_rnd() << 8) ^ (int32_t)test_rnd();
                if (rep == 1 && t < 4) {
                    v = t & 1 ? INT32_MAX - 5 : INT32_MIN + 5;
                }
                y[t * 16 + i] = ref[t * 16 + i] = v;
            }
        }
        for (int t = 0; t < T; t++) {
            for (int i = 0; i < 16; i++) {
                int32_t acc = 32;
                for (int k = 0; k < G; k++) {
                    acc += w[k * 16 + i] * x[t * G + k];
                }
                const int32_t p = ((acc >> 6) * (int32_t)n16[i]) >> sar[t];
                const int64_t s = (int64_t)ref[t * 16 + i] + p;
                ref[t * 16 + i] = s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : (int32_t)s;
            }
        }
        pq_tile16_n(w, x, G, T, G, n16, sar, y);
        for (int i = 0; i < T * 16; i++) {
            if (y[i] != ref[i]) {
                if (first < 0) {
                    first = i;
                    report("tile16_n rep %d lane %d (t %d sar %d): got %ld want %ld", rep, i, i / 16, sar[i / 16],
                           (long)y[i], (long)ref[i]);
                }
                bad++;
            }
        }
    }
    report("tile16_n: %d wrong of %d", bad, 8 * T * 16);
    int32_t *acc = heap_caps_aligned_alloc(16, T * 16 * 4, MALLOC_CAP_INTERNAL);
    if (acc) {
        uint32_t c0, c1, c2;
        __asm__ volatile("csrr %0, mcycle" : "=r"(c0));
        pq_tile16(w, x, G, T, G, acc);
        __asm__ volatile("csrr %0, mcycle" : "=r"(c1));
        pq_tile16_n(w, x, G, T, G, n16, sar, y);
        __asm__ volatile("csrr %0, mcycle" : "=r"(c2));
        report("per frame (G 128): pq_tile16 %lu cycles, pq_tile16_n %lu cycles", (unsigned long)((c1 - c0) / T),
               (unsigned long)((c2 - c1) / T));
        free(acc);
    }
    free(w);
    free(x);
    free(n16);
    free(y);
    free(ref);
}

// pq_q8_shift (PIE srcmb, ties to even) against its C model: shifts 0..20, exact ties, saturation edges
static void test_q8_shift(void)
{
    int32_t *r = heap_caps_aligned_alloc(16, 128 * 4, MALLOC_CAP_INTERNAL);
    int8_t *q = heap_caps_aligned_alloc(16, 128, MALLOC_CAP_INTERNAL);
    int bad = 0;
    for (int e = 0; e <= 20; e++) {
        for (int rep = 0; rep < 4; rep++) {
            for (int k = 0; k < 128; k++) {
                int32_t lim = 1 << (e + 7);
                int32_t v = (int32_t)(test_rnd() % (2 * lim - 1)) - (lim - 1);
                if (rep == 1 && e > 0) {   // exact ties: odd and even multiples of 2^(e-1)
                    v = ((int32_t)(test_rnd() % 255) - 127) * (1 << e) + (1 << (e - 1)) * (k & 1 ? 1 : -1);
                }
                if (rep == 2) {            // the edges
                    v = k & 1 ? lim - 1 : -(lim - 1);
                }
                r[k] = v;
            }
            pq_q8_shift(r, 128, e, q);
            for (int k = 0; k < 128; k++) {
                int32_t v = r[k];
                if (e > 0) {
                    const int32_t f = v >> e, rem = v - (int32_t)((uint32_t)f << e), half = 1 << (e - 1);
                    v = f + (rem > half || (rem == half && (f & 1)));
                }
                v = v > 127 ? 127 : v < -128 ? -128 : v;
                if (q[k] != v) {
                    if (!bad) {
                        report("q8_shift e %d rep %d: r %ld -> %d, want %ld", e, rep, (long)r[k], q[k], (long)v);
                    }
                    bad++;
                }
            }
        }
    }
    report("q8_shift: %d wrong of %d", bad, 21 * 4 * 128);
    free(r);
    free(q);
}


// pq_fwht_i32 / pq_fwht_i16 (PIE) against a plain Hadamard: G 16..128; int16 inputs random and extreme, int32
// inputs random at several sizes; the bit length of the returned max; times at G 128
static void test_fwht(void)
{
    int32_t *r = heap_caps_aligned_alloc(16, 128 * 4, MALLOC_CAP_INTERNAL);
    int16_t *x = heap_caps_aligned_alloc(16, 128 * 2, MALLOC_CAP_INTERNAL);
    int32_t ref[128];
    int bad = 0, badm = 0, tests = 0;
    for (int G = 16; G <= 128; G *= 2) {
        for (int rep = 0; rep < 6; rep++) {
            for (int k = 0; k < G; k++) {   // rep 0..3: int16 inputs; 4, 5: int32 inputs (|v| < 2^23, 2^20)
                x[k] = rep == 0 ? 32767 : rep == 1 ? (k & 1 ? -32768 : 32767) : (int16_t)test_rnd();
                ref[k] = r[k] = rep < 4 ? x[k] : (int32_t)(test_rnd() << 8) >> (8 + rep - 3);
            }
            for (int h = 1; h < G; h *= 2) {
                for (int i = 0; i < G; i += 2 * h) {
                    for (int j = i; j < i + h; j++) {
                        const int32_t u = ref[j], v = ref[j + h];
                        ref[j] = u + v;
                        ref[j + h] = u - v;
                    }
                }
            }
            if (rep < 4) {
                memset(r, 0x55, 128 * 4);
            }
            const uint32_t m = rep < 4 ? pq_fwht_i16(x, r, G) : pq_fwht_i32(r, G);
            uint32_t mx = 0;
            int b0 = bad;
            for (int k = 0; k < G; k++) {
                bad += r[k] != ref[k];
                const uint32_t u = ref[k] < 0 ? -(uint32_t)ref[k] : (uint32_t)ref[k];
                mx = u > mx ? u : mx;
            }
            badm += (m ? 32 - __builtin_clz(m) : 0) != (mx ? 32 - __builtin_clz(mx) : 0);
            if (bad > b0 && b0 == 0) {
                report("fwht G %d rep %d: r[0..3] %ld %ld %ld %ld, want %ld %ld %ld %ld", G, rep, (long)r[0],
                       (long)r[1], (long)r[2], (long)r[3], (long)ref[0], (long)ref[1], (long)ref[2],
                       (long)ref[3]);
            }
            tests++;
        }
    }
    const uint32_t c0 = cycles_now();
    for (int i = 0; i < 100; i++) {
        pq_fwht_i16(x, r, 128);
    }
    const uint32_t c1 = cycles_now();
    for (int i = 0; i < 100; i++) {
        pq_fwht_i32(r, 128);
    }
    const uint32_t c2 = cycles_now();
    report("fwht: %d wrong values, %d wrong max of %d tests; G 128: i16 %lu, i32 %lu cycles", bad, badm, tests,
           (unsigned long)(c1 - c0) / 100, (unsigned long)(c2 - c1) / 100);
    free(r);
    free(x);
}

// pq_out_tab16 (PIE) against pq_tab_eval: random table, y32 over the whole int32 range and near the int16 edges
static void test_out_tab16(void)
{
    const int T = 64, ldh = 40;
    uint32_t *tab = heap_caps_aligned_alloc(16, PQ_TAB_N * 4, MALLOC_CAP_INTERNAL);
    int32_t *y = heap_caps_aligned_alloc(16, T * 16 * 4, MALLOC_CAP_INTERNAL);
    int16_t *h = heap_caps_aligned_alloc(16, T * ldh * 2, MALLOC_CAP_INTERNAL);
    for (int j = 0; j < PQ_TAB_N; j++) {
        tab[j] = test_rnd() ^ (test_rnd() << 16);
    }
    int bad = 0;
    for (int rep = 0; rep < 4; rep++) {
        for (int i = 0; i < T * 16; i++) {
            const int32_t v = (int32_t)(test_rnd() ^ (test_rnd() << 16));
            y[i] = rep == 0   ? v
                   : rep == 1 ? v >> 7
                   : rep == 2 ? (v >> 16) + (i & 1 ? 32767 * 256 : -32768 * 256)
                              : (int32_t)(test_rnd() % 65536 - 32768) * 256 + (i & 255);
        }
        memset(h, 0, T * ldh * 2);
        pq_out_tab16(y, T, tab, h, ldh);
        for (int t = 0; t < T; t++) {
            for (int i = 0; i < 16; i++) {
                int32_t v = y[t * 16 + i] >> 8;
                v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
                const int16_t want = pq_tab_eval(tab, v);
                if (h[t * ldh + i] != want) {
                    if (!bad) {
                        report("out_tab16 rep %d t %d i %d: y %ld -> %d, want %d", rep, t, i, (long)y[t * 16 + i],
                               h[t * ldh + i], want);
                    }
                    bad++;
                }
            }
        }
    }
    const uint32_t c0 = cycles_now();
    pq_out_tab16(y, T, tab, h, ldh);
    const uint32_t c1 = cycles_now();
    report("out_tab16: %d wrong of %d; %lu cycles per 16 values", bad, 4 * T * 16, (unsigned long)(c1 - c0) / T);
    free(tab);
    free(y);
    free(h);
}

// pq_out_add16 (PIE) against its C model: n 1..24, every j with 1 <= n - j <= 14, int32 and int16 saturation
static void test_out_add16(void)
{
    const int T = 16, ldx = 24;
    int32_t *y = heap_caps_aligned_alloc(16, T * 16 * 4, MALLOC_CAP_INTERNAL);
    int16_t *x = heap_caps_aligned_alloc(16, T * ldx * 2, MALLOC_CAP_INTERNAL);
    int16_t *ref = malloc(T * ldx * 2);
    int bad = 0, tests = 0;
    for (int n = 1; n <= 24; n++) {
        for (int j = n - 14 < 0 ? 0 : n - 14; j < n; j++) {
            for (int i = 0; i < T * 16; i++) {
                const int32_t v = (int32_t)(test_rnd() ^ (test_rnd() << 16));
                y[i] = i % 7 == 0 ? v : v >> (31 - (n + 15 < 31 ? n + 15 : 31) + (int)(test_rnd() % 3));
            }
            for (int i = 0; i < T * ldx; i++) {
                x[i] = (int16_t)test_rnd();
                ref[i] = x[i];
            }
            pq_out_add16(y, T, n, j, x, ldx);
            for (int t = 0; t < T; t++) {
                for (int i = 0; i < ldx; i++) {
                    int16_t want = ref[t * ldx + i];
                    if (i < 16) {
                        int64_t s = (int64_t)ref[t * ldx + i] * (1 << (n - j)) + y[t * 16 + i];
                        s = s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : s;
                        const int32_t v = (int32_t)s >> n;
                        want = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                    }
                    if (x[t * ldx + i] != want) {
                        if (!bad) {
                            report("out_add16 n %d j %d t %d i %d: x %d y %ld -> %d, want %d", n, j, t, i,
                                   ref[t * ldx + i], (long)y[t * 16 + i], x[t * ldx + i], want);
                        }
                        bad++;
                    }
                }
            }
            tests++;
        }
    }
    const uint32_t c0 = cycles_now();
    pq_out_add16(y, T, 10, 0, x, ldx);
    const uint32_t c1 = cycles_now();
    report("out_add16: %d wrong in %d tests; %lu cycles per 16 values", bad, tests, (unsigned long)(c1 - c0) / T);
    free(y);
    free(x);
    free(ref);
}

// pq_sumsq16 / pq_scale_bypass16 (PIE) against their C models: random rows, extreme values, every k
static void test_norm16(void)
{
    const int C = 512;
    int16_t *x = heap_caps_aligned_alloc(16, C * 2, MALLOC_CAP_INTERNAL);
    int16_t *xo = heap_caps_aligned_alloc(16, C * 2, MALLOC_CAP_INTERNAL);
    int16_t *b = heap_caps_aligned_alloc(16, C * 2, MALLOC_CAP_INTERNAL);
    int16_t *w = heap_caps_aligned_alloc(16, C * 2, MALLOC_CAP_INTERNAL);
    int16_t *o = heap_caps_aligned_alloc(16, C * 2, MALLOC_CAP_INTERNAL);
    int bad_s = 0, bad_b = 0;
    for (int rep = 0; rep < 32; rep++) {
        for (int c = 0; c < C; c++) {
            x[c] = rep == 0 ? 32767 : rep == 1 ? -32768 : (int16_t)test_rnd();
            xo[c] = (int16_t)test_rnd();
            b[c] = rep < 2 ? (int16_t)(rep == 0 ? -32768 : 32767) : (int16_t)(test_rnd() % 16385);
            w[c] = (int16_t)(16384 - (rep < 2 ? 16384 : b[c]));
        }
        uint32_t want = 0;
        for (int c = 0; c < C; c++) {
            int32_t d = (int32_t)x[c] - b[c];
            d = d > 32767 ? 32767 : d < -32768 ? -32768 : d;
            want += (uint32_t)(d * d) >> 8;
        }
        bad_s += pq_sumsq16(x, b, C) != want;
        if (rep < 2) {   // (b as bypass scales needs 0..16384)
            for (int c = 0; c < C; c++) {
                b[c] = 16384;
            }
        }
        const int A = rep == 2 ? 1 : (int)(test_rnd() % 32768), k = rep == 2 ? 0 : rep % 31;
        pq_scale_bypass16(x, xo, C, A, k, b, w, o);
        for (int c = 0; c < C; c++) {
            int32_t n = ((int32_t)x[c] * A) >> k;
            n = n > 32767 ? 32767 : n < -32768 ? -32768 : n;
            int32_t v = ((int32_t)xo[c] * w[c] + n * b[c] + 8192) >> 14;
            v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
            if (o[c] != v) {
                if (!bad_b) {
                    report("scale_bypass16 rep %d c %d: x %d xo %d A %d k %d b %d -> %d, want %ld", rep, c, x[c],
                           xo[c], A, k, b[c], o[c], (long)v);
                }
                bad_b++;
            }
        }
    }
    const uint32_t c0 = cycles_now();
    pq_sumsq16(x, b, C);
    const uint32_t c1 = cycles_now();
    pq_scale_bypass16(x, xo, C, 1000, 12, b, w, o);
    const uint32_t c2 = cycles_now();
    report("sumsq16: %d wrong, scale_bypass16: %d wrong of %d; C 512: %lu, %lu cycles", bad_s, bad_b, 32 * C,
           (unsigned long)(c1 - c0), (unsigned long)(c2 - c1));
    free(x);
    free(xo);
    free(b);
    free(w);
    free(o);
}

static int32_t sat16t(int32_t v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

// pq_out_act8 (PIE) against its C model: random tiles and parameters, with and without a table
static void test_out_act8(void)
{
    const int T = 32, ldy = 48;
    int32_t *acc = heap_caps_aligned_alloc(16, T * 16 * 4, MALLOC_CAP_INTERNAL);
    int16_t *M = heap_caps_aligned_alloc(16, 64, MALLOC_CAP_INTERNAL), *N = M + 16;
    int32_t *B = heap_caps_aligned_alloc(16, 64, MALLOC_CAP_INTERNAL);
    uint32_t *tab = heap_caps_aligned_alloc(16, 4 << (16 - PQ_ACT8_K), MALLOC_CAP_INTERNAL);
    int8_t *y = heap_caps_aligned_alloc(16, T * ldy, MALLOC_CAP_INTERNAL);
    for (int j = 0; j < 1 << (16 - PQ_ACT8_K); j++) {
        tab[j] = test_rnd() ^ (test_rnd() << 16);
    }
    int bad = 0, tests = 0;
    for (int rep = 0; rep < 40; rep++) {
        const int r = test_rnd() % 12, sh = 4 + test_rnd() % 14, sh2 = test_rnd() % 20, use_tab = rep & 1;
        for (int i = 0; i < 16; i++) {
            M[i] = (int16_t)test_rnd();
            N[i] = (int16_t)(test_rnd() % 32768);
            B[i] = (int32_t)(test_rnd() ^ (test_rnd() << 16)) >> (test_rnd() % 16);
        }
        for (int i = 0; i < T * 16; i++) {
            acc[i] = (int32_t)(test_rnd() ^ (test_rnd() << 16)) >> (test_rnd() % 24);
        }
        memset(y, 0x5a, T * ldy);
        pq_out_act8(acc, T, M, B, N, r, sh, sh2, use_tab ? tab : NULL, y, ldy);
        for (int t = 0; t < T; t++) {
            for (int i = 0; i < 16; i++) {
                const int32_t a = sat16t(acc[t * 16 + i] >> r);
                int64_t s = (int64_t)a * M[i] + B[i];
                s = s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : s;
                const int32_t v = sat16t((int32_t)s >> sh);
                const int32_t h = use_tab ? pq_tab_eval_k(tab, v, PQ_ACT8_K) : v;
                const int32_t m = h * N[i];
                int32_t q = m;
                if (sh2 > 0) {
                    const int32_t u = m < 0 ? -m : m;
                    q = (u + (1 << (sh2 - 1))) >> sh2;
                    q = m < 0 ? -q : q;
                }
                q = sat16t(q);
                q = q > 127 ? 127 : q < -127 ? -127 : q;
                if (y[t * ldy + i] != q) {
                    if (!bad) {
                        report("out_act8 rep %d t %d i %d: acc %ld r %d sh %d sh2 %d tab %d -> %d, want %ld", rep,
                               t, i, (long)acc[t * 16 + i], r, sh, sh2, use_tab, y[t * ldy + i], (long)q);
                    }
                    bad++;
                }
            }
        }
        tests++;
    }
    const uint32_t c0 = cycles_now();
    pq_out_act8(acc, T, M, B, N, 5, 10, 16, tab, y, ldy);
    const uint32_t c1 = cycles_now();
    report("out_act8: %d wrong in %d tests of %d values; %lu cycles per 16 values (table)", bad, tests, T * 16,
           (unsigned long)(c1 - c0) / T);
    free(acc);
    free(M);
    free(B);
    free(tab);
    free(y);
}

static int32_t rsh_away(int32_t v, int s)
{
    if (s <= 0) {
        return v;
    }
    const int32_t u = v < 0 ? -v : v, q = (u + (1 << (s - 1))) >> s;
    return v < 0 ? -q : q;
}

// pq_mul_q8, pq_dwcomb16 and pq_out_tab16 without a table (PIE) against their C models
static void test_conv16(void)
{
    const int T = 24, lda = 40, ldy = 48;
    int16_t *a = heap_caps_aligned_alloc(16, T * lda * 2, MALLOC_CAP_INTERNAL);
    int16_t *b = heap_caps_aligned_alloc(16, T * lda * 2, MALLOC_CAP_INTERNAL);
    int16_t *N = heap_caps_aligned_alloc(16, 32, MALLOC_CAP_INTERNAL);
    int8_t *y8 = heap_caps_aligned_alloc(16, T * ldy, MALLOC_CAP_INTERNAL);
    int32_t *ac = heap_caps_aligned_alloc(16, T * 64, MALLOC_CAP_INTERNAL);
    int32_t *ak = heap_caps_aligned_alloc(16, T * 64, MALLOC_CAP_INTERNAL);
    int16_t *es = heap_caps_aligned_alloc(16, T * 32, MALLOC_CAP_INTERNAL);
    int16_t *y16 = heap_caps_aligned_alloc(16, T * ldy * 2, MALLOC_CAP_INTERNAL);
    pq_comb_t *P = heap_caps_aligned_alloc(16, sizeof(pq_comb_t), MALLOC_CAP_INTERNAL);
    uint32_t *tab = heap_caps_aligned_alloc(16, PQ_TAB_N * 4, MALLOC_CAP_INTERNAL);
    for (int j = 0; j < PQ_TAB_N; j++) {
        tab[j] = test_rnd() ^ (test_rnd() << 16);
    }
    int bad_m = 0, bad_c = 0, bad_i = 0;
    for (int rep = 0; rep < 24; rep++) {
        const int sh2 = test_rnd() % 20, r = test_rnd() % 12, sh = 4 + test_rnd() % 14;
        for (int i = 0; i < T * lda; i++) {
            a[i] = (int16_t)test_rnd();
            b[i] = rep == 0 ? 32767 : (int16_t)(test_rnd() % 32768);
        }
        for (int i = 0; i < 16; i++) {
            N[i] = (int16_t)(test_rnd() % 32768);
            P->Mc[i] = (int16_t)test_rnd();
            P->Mk[i] = (int16_t)test_rnd();
            P->Bc[i] = (int32_t)(test_rnd() ^ (test_rnd() << 16)) >> (test_rnd() % 16);
            P->Bk[i] = (int32_t)(test_rnd() ^ (test_rnd() << 16)) >> (test_rnd() % 16);
        }
        for (int i = 0; i < T * 16; i++) {
            ac[i] = (int32_t)(test_rnd() ^ (test_rnd() << 16)) >> (test_rnd() % 24);
            ak[i] = (int32_t)(test_rnd() ^ (test_rnd() << 16)) >> (test_rnd() % 24);
            es[i] = (int16_t)(test_rnd() % 32768);
        }
        pq_mul_q8(a, b, lda, T, N, sh2, y8, ldy);
        pq_dwcomb16(ac, ak, T, P, r, sh, es, tab, y16, ldy);
        for (int t = 0; t < T; t++) {
            for (int i = 0; i < 16; i++) {
                const int32_t g = sat16t(rsh_away((int32_t)a[t * lda + i] * b[t * lda + i], 15));
                int32_t q = sat16t(rsh_away(g * N[i], sh2));
                q = q > 127 ? 127 : q < -127 ? -127 : q;
                if (y8[t * ldy + i] != q) {
                    if (!bad_m) {
                        report("mul_q8 rep %d t %d i %d: %d * %d N %d sh2 %d -> %d, want %ld", rep, t, i,
                               a[t * lda + i], b[t * lda + i], N[i], sh2, y8[t * ldy + i], (long)q);
                    }
                    bad_m++;
                }
                int64_t c = (int64_t)sat16t(ac[t * 16 + i] >> r) * P->Mc[i] + P->Bc[i];
                int64_t k = (int64_t)sat16t(ak[t * 16 + i] >> r) * P->Mk[i] + P->Bk[i];
                c = c > INT32_MAX ? INT32_MAX : c < INT32_MIN ? INT32_MIN : c;
                k = k > INT32_MAX ? INT32_MAX : k < INT32_MIN ? INT32_MIN : k;
                const int32_t yc = sat16t((int32_t)c >> sh), yk = sat16t((int32_t)k >> sh);
                const int32_t v = sat16t(yc + sat16t(rsh_away(yk * es[t * 16 + i], 11)));
                const int16_t want = pq_tab_eval(tab, v);
                if (y16[t * ldy + i] != want) {
                    if (!bad_c) {
                        report("dwcomb16 rep %d t %d i %d: yc %ld yk %ld es %d -> %d, want %d", rep, t, i,
                               (long)yc, (long)yk, es[t * 16 + i], y16[t * ldy + i], want);
                    }
                    bad_c++;
                }
            }
        }
        pq_out_tab16(ac, T, NULL, y16, ldy);
        for (int t = 0; t < T; t++) {
            for (int i = 0; i < 16; i++) {
                bad_i += y16[t * ldy + i] != sat16t(ac[t * 16 + i] >> 8);
            }
        }
    }
    uint32_t c0 = cycles_now();
    pq_mul_q8(a, b, lda, T, N, 12, y8, ldy);
    uint32_t c1 = cycles_now();
    pq_dwcomb16(ac, ak, T, P, 5, 10, es, tab, y16, ldy);
    uint32_t c2 = cycles_now();
    report("mul_q8: %d wrong, dwcomb16: %d wrong, out_tab16 identity: %d wrong (of %d); cycles per 16: %lu, %lu",
           bad_m, bad_c, bad_i, 24 * T * 16, (unsigned long)(c1 - c0) / T, (unsigned long)(c2 - c1) / T);
    free(a);
    free(b);
    free(N);
    free(y8);
    free(ac);
    free(ak);
    free(es);
    free(y16);
    free(P);
    free(tab);
}

// pq_mul16 and pq_lane_q8 (PIE) against their C models
static void test_cvd(void)
{
    const int T = 40, lda = 48, ldy = 32;
    int16_t *a = heap_caps_aligned_alloc(16, T * lda * 2, MALLOC_CAP_INTERNAL);
    int16_t *b = heap_caps_aligned_alloc(16, T * lda * 2, MALLOC_CAP_INTERNAL);
    int16_t *g = heap_caps_aligned_alloc(16, T * 32, MALLOC_CAP_INTERNAL);
    int16_t *N = heap_caps_aligned_alloc(16, 32, MALLOC_CAP_INTERNAL);
    int8_t *y = heap_caps_aligned_alloc(16, T * ldy, MALLOC_CAP_INTERNAL);
    int bad_m = 0, bad_l = 0;
    for (int rep = 0; rep < 16; rep++) {
        for (int i = 0; i < T * lda; i++) {
            a[i] = (int16_t)test_rnd();
            b[i] = rep == 0 ? -32768 : (int16_t)test_rnd();
        }
        for (int i = 0; i < 16; i++) {
            N[i] = (int16_t)(1 << (15 - (int)(test_rnd() % 9)));
            N[i] = N[i] == -32768 ? 32767 : N[i];
        }
        pq_mul16(a, b, lda, T, g);
        for (int t = 0; t < T; t++) {
            for (int i = 0; i < 16; i++) {
                const int32_t want = sat16t(rsh_away((int32_t)a[t * lda + i] * b[t * lda + i], 15));
                bad_m += g[t * 16 + i] != want;
            }
        }
        pq_lane_q8(g, T, N, y, ldy);
        for (int t = 0; t < T; t++) {
            for (int i = 0; i < 16; i++) {
                int32_t q = sat16t(rsh_away((int32_t)g[t * 16 + i] * N[i], 15));
                q = q > 127 ? 127 : q < -127 ? -127 : q;
                bad_l += y[t * ldy + i] != q;
            }
        }
    }
    int bad_o = 0;
    for (int rep = 0; rep < 16; rep++) {
        for (int i = 0; i < T * 16; i++) {
            g[i] = rep == 0   ? -32768
                   : rep == 1 ? (int16_t)(i & 1 ? -1 : 0)
                              : (int16_t)((int16_t)test_rnd() >> (test_rnd() % 15));
        }
        int32_t m[16], want[16] = { 0 };
        pq_lane_or16(g, T, m);
        for (int t = 0; t < T; t++) {
            for (int i = 0; i < 16; i++) {
                const int32_t v = g[t * 16 + i];
                want[i] |= (v ^ (v >> 15)) & 0xffff;
            }
        }
        for (int i = 0; i < 16; i++) {
            bad_o += m[i] != want[i];
        }
    }
    report("mul16: %d wrong, lane_q8: %d wrong (of %d); lane_or16: %d wrong of 256", bad_m, bad_l, 16 * T * 16,
           bad_o);
    free(a);
    free(b);
    free(g);
    free(N);
    free(y);
}

void test_int_kernels(void)
{
    test_cvd();
    test_conv16();
    test_out_act8();
    test_norm16();
    test_out_add16();
    test_fwht();
    test_out_tab16();
    test_q8_shift();
    test_tile16_n();
}
