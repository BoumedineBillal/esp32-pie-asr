// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// parallel.h on the host: the parts run one after the other (the same outputs as on the chip's two cores).
#include "runtime/parallel.h"

void pq_par_init(void)
{
}

void pq_par_run(pq_par_fn fn, void *ctx)
{
    for (int p = 0; p < PQ_PAR_PARTS; p++) {
        fn(ctx, p, PQ_PAR_PARTS);
    }
}
