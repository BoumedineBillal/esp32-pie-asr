// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The kernel test firmware: every PIE kernel against its C model on the chip (bit-exact; each prints
// "<kernel>: <n> wrong ..."), then their cycle counts. "kernels start" / "kernels done" frame the output.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kernels/kernels.h"
#include "kernels/linear.h"
#include "test_common.h"

static uint32_t rng = 12345;

uint32_t test_rnd(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng >> 8;
}

// the decode and tile kernels on random data (and the largest sums), the depthwise kernel on the same data
static void test_kernels(void)
{
    pq_pie_init();
    uint8_t *codes = heap_caps_aligned_alloc(16, 1024, MALLOC_CAP_INTERNAL);
    int8_t *out = heap_caps_aligned_alloc(16, 4096 + 64, MALLOC_CAP_INTERNAL);
    int8_t *x = heap_caps_aligned_alloc(16, 64 * 128 + 64, MALLOC_CAP_INTERNAL);
    int32_t *acc = heap_caps_aligned_alloc(16, 64 * 16 * 4, MALLOC_CAP_INTERNAL);
    uint32_t lut2[256], lut4[256];
    for (int b = 0; b < 256; b++) {
        lut2[b] = 0;
        for (int r = 0; r < 4; r++) {
            lut2[b] |= (uint32_t)(uint8_t)pq_lq2[(b >> (2 * r)) & 3] << (8 * r);
        }
        lut4[b] = (uint32_t)(uint8_t)pq_lq4[b & 15] | (uint32_t)(uint8_t)pq_lq4[b >> 4] << 8;
    }
    // decode2: 512 code bytes (one 2-bit tile, G = 128)
    for (int i = 0; i < 512; i++) {
        codes[i] = test_rnd();
    }
    pq_decode2(codes, 512, lut2, out);
    int bad = 0;
    for (int i = 0; i < 512; i++) {
        for (int r = 0; r < 4; r++) {
            bad += out[4 * i + r] != pq_lq2[(codes[i] >> (2 * r)) & 3];
        }
    }
    report("decode2: %d wrong of 2048", bad);
    for (int i = 0; i < 1024; i++) {
        codes[i] = test_rnd();
    }
    pq_decode4(codes, 1024, lut4, out);
    bad = 0;
    for (int i = 0; i < 1024; i++) {
        bad += out[2 * i] != pq_lq4[codes[i] & 15];
        bad += out[2 * i + 1] != pq_lq4[codes[i] >> 4];
    }
    report("decode4: %d wrong of 2048", bad);
    if (bad) {
        report("decode4 first bytes: %d %d %d %d | want %d %d %d %d", out[0], out[1], out[2], out[3],
               pq_lq4[codes[0] & 15], pq_lq4[codes[0] >> 4], pq_lq4[codes[1] & 15], pq_lq4[codes[1] >> 4]);
    }
    // tile16: G in 16..128, T = 7, extreme values too
    for (int G = 16; G <= 128; G *= 2) {
        for (int i = 0; i < G * 16; i++) {
            out[i] = (int8_t)(test_rnd() % 255 - 127);
        }
        for (int i = 0; i < 7 * 128; i++) {
            x[i] = (int8_t)(test_rnd() % 255 - 127);
        }
        if (G == 128) {   // the largest possible sums
            memset(out, 127, 128 * 16);
            memset(x, 127, 128);
            memset(x + 128, -127, 128);
        }
        pq_tile16(out, x, 128, 7, G, acc);
        bad = 0;
        int first = -1;
        for (int t = 0; t < 7; t++) {
            for (int i = 0; i < 16; i++) {
                int32_t a = 0;
                for (int k = 0; k < G; k++) {
                    a += out[k * 16 + i] * x[t * 128 + k];
                }
                if (a != acc[t * 16 + i]) {
                    bad++;
                    if (first < 0) {
                        first = t * 16 + i;
                    }
                }
            }
        }
        report("tile16 G %d: %d wrong of 112 (acc[0] %ld)", G, bad, (long)acc[0]);
        if (G == 128) {   // pq_dw16 on the same data: 7 frames, 9 taps at offsets 16*k*3, frame step 32
            int32_t off[9];
            for (int k = 0; k < 9; k++) {
                off[k] = 48 * k;
            }
            pq_dw16(x, 32, 7, off, out, 9, acc);
            int bad2 = 0;
            for (int t = 0; t < 7; t++) {
                for (int i = 0; i < 16; i++) {
                    int32_t a = 0;
                    for (int k = 0; k < 9; k++) {
                        a += out[k * 16 + i] * x[t * 32 + off[k] + i];
                    }
                    bad2 += a != acc[t * 16 + i];
                }
            }
            report("dw16: %d wrong of 112", bad2);
        }
        if (bad) {
            report("  first wrong lane %d: got %ld; lanes 0..3 %ld %ld %ld %ld", first, (long)acc[first],
                   (long)acc[0], (long)acc[1], (long)acc[2], (long)acc[3]);
        }
    }
    free(codes);
    free(out);
    free(x);
    free(acc);
}

// cycles of the kernels on internal-RAM data: decode of one 2-bit / 4-bit tile (G = 128), one frame of tile16
static void bench_kernels(void)
{
    pq_pie_init();
    uint8_t *codes = heap_caps_aligned_alloc(16, 1024, MALLOC_CAP_INTERNAL);
    int8_t *out = heap_caps_aligned_alloc(16, 2048 + 64, MALLOC_CAP_INTERNAL);
    int8_t *x = heap_caps_aligned_alloc(16, 64 * 128 + 64, MALLOC_CAP_INTERNAL);
    int32_t *acc = heap_caps_aligned_alloc(16, 64 * 16 * 4, MALLOC_CAP_INTERNAL);
    static uint32_t lut[256] __attribute__((aligned(16)));
    for (int i = 0; i < 256; i++) {
        lut[i] = test_rnd();
    }
    for (int i = 0; i < 1024; i++) {
        codes[i] = test_rnd();
    }
    const int N = 200;
    uint32_t c0 = cycles_now();
    for (int i = 0; i < N; i++) {
        pq_decode2(codes, 512, lut, out);
    }
    uint32_t c1 = cycles_now();
    for (int i = 0; i < N; i++) {
        pq_decode2_il(codes, 512, lut, out);
    }
    uint32_t c2 = cycles_now();
    for (int i = 0; i < N; i++) {
        pq_decode4(codes, 1024, lut, out);
    }
    uint32_t c3 = cycles_now();
    for (int i = 0; i < N; i++) {
        pq_tile16(out, x, 128, 64, 128, acc);
    }
    uint32_t c4 = cycles_now();
    report("per 2048 weights: decode2 %lu cycles, decode2 interleaved %lu, decode4 %lu; tile16 per frame (G 128) %lu "
           "(%.1f MAC/cycle)", (unsigned long)((c1 - c0) / N), (unsigned long)((c2 - c1) / N),
           (unsigned long)((c3 - c2) / N), (unsigned long)((c4 - c3) / N / 64),
           2048.0 * 64 * N / (double)(c4 - c3));
    {   // the input quantization of one 128-group: Hadamard, then max + scale + round
        float r[128];
        int8_t q[128];
        for (int i = 0; i < 128; i++) {
            r[i] = (float)((int)(test_rnd() % 2001) - 1000) / 500.0f;
        }
        uint32_t d0 = cycles_now();
        for (int i = 0; i < N; i++) {
            pq_fwht_f32(r, 128);
        }
        uint32_t d1 = cycles_now();
        volatile float sink = 0;
        for (int i = 0; i < N; i++) {
            float m = 0.0f;
            for (int k = 0; k < 128; k++) {
                float a = fabsf(r[k]);
                m = a > m ? a : m;
            }
            float inv = m > 0.0f ? 127.0f / m : 0.0f;
            for (int k = 0; k < 128; k++) {
                q[k] = (int8_t)pq_round(r[k] * inv);
            }
            sink += q[i & 127];
        }
        uint32_t d2 = cycles_now();
        report("per 128 inputs: fwht %lu cycles, max+round %lu cycles", (unsigned long)((d1 - d0) / N),
               (unsigned long)((d2 - d1) / N));
    }
    free(codes);
    free(out);
    free(x);
    free(acc);
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("kernels start\n");
    test_int_kernels();
    test_kernels();
    bench_kernels();
    printf("kernels done\n");
}
