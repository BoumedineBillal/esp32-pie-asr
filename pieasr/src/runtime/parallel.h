// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// Two-core work splitting. pq_par_run(fn, ctx) calls fn(ctx, 0, 2) and fn(ctx, 1, 2) and returns when both are done:
// on the chip part 1 runs on core 1 (parallel_esp32.c); on the host (parallel_host.c) the parts run one after the
// other. A part only writes its own outputs, so the results do not depend on where it ran.
#pragma once
#include <stdint.h>

#define PQ_PAR_PARTS 2

typedef void (*pq_par_fn)(void *ctx, int part, int parts);

void pq_par_init(void);   // starts the worker (idempotent)
void pq_par_run(pq_par_fn fn, void *ctx);
// the range [lo, hi) of part `part` of n items, in multiples of `align`
static inline void pq_par_range(int n, int part, int parts, int align, int *lo, int *hi)
{
    int units = (n + align - 1) / align;
    int a = units * part / parts, b = units * (part + 1) / parts;
    *lo = a * align < n ? a * align : n;
    *hi = b * align < n ? b * align : n;
}

// chip only: 1 runs both parts on the caller (to measure the second core's gain); the calls and core 0's cycles
// waiting for core 1
extern int pq_par_single;
extern uint32_t pq_par_calls, pq_par_wait_cycles;
