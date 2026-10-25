// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The accuracy of math/detmath.h against the host's libm: the worst relative error of the double functions (below
// 1e-12), their float results at most 1 ulp from libm's, dm_logf at most 2 ulp from logf.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "math/detmath.h"

static double worst;
static long nf, nf_ulp1, nf_more, n;
static int fails;

static void cmp(double got, double want)
{
    double rel = fabs(got - want) / (fabs(want) > 1e-300 ? fabs(want) : 1e-300);
    if (rel > worst) {
        worst = rel;
    }
    float fg = (float)got, fw = (float)want;
    n++;
    if (fg != fw) {
        nf++;
        if (fabsf(fg - fw) <= fabsf(nextafterf(fw, 2.0f * fw + 1.0f) - fw) * 1.0001f) {
            nf_ulp1++;
        } else {
            nf_more++;
        }
    }
}

static void report(const char *name)
{
    const int ok = worst < 1e-12 && nf_more == 0;
    fails += !ok;
    printf("%s %-8s %8ld values: worst relative %.2e; float differs %ld (1 ulp %ld, more %ld)\n", ok ? "PASS" : "FAIL",
           name, n, worst, nf, nf_ulp1, nf_more);
    worst = 0;
    nf = nf_ulp1 = nf_more = n = 0;
}

int main(void)
{
    srand(1);
    for (int i = 0; i < 400000; i++) {
        double x = -80.0 + 100.0 * rand() / (double)RAND_MAX;
        cmp(dm_exp(x), exp(x));
    }
    report("exp");
    for (int i = 0; i < 400000; i++) {
        double x = exp(-60.0 + 120.0 * rand() / (double)RAND_MAX);
        cmp(dm_log(x), log(x));
    }
    report("log");
    for (int i = 0; i < 400000; i++) {
        double y = exp(-40.0 + 45.0 * rand() / (double)RAND_MAX);
        cmp(dm_log1p(y), log1p(y));
    }
    report("log1p");
    for (int i = 0; i < 400000; i++) {
        double x = -12.0 + 24.0 * rand() / (double)RAND_MAX;
        cmp(dm_tanh(x), tanh(x));
    }
    report("tanh");
    for (int i = 0; i < 400000; i++) {
        double x = -7.0 + 14.0 * rand() / (double)RAND_MAX;
        cmp(dm_sin(x), sin(x));
        cmp(dm_cos(x), cos(x));
    }
    report("sin/cos");
    for (int i = 0; i < 400000; i++) {
        double a = rand() / (double)RAND_MAX;
        cmp(dm_pow(a, 0.85), pow(a, 0.85));
    }
    report("pow.85");
    for (int i = 0; i < 400000; i++) {
        double x = (rand() & 1 ? 1.0 : -1.0) * exp(-12.0 + 20.0 * rand() / (double)RAND_MAX);
        cmp(dm_atan(x), atan(x));
    }
    report("atan");
    long nd = 0, worst_ulp = 0;
    for (int i = 0; i < 2000000; i++) {
        float x = (float)exp(-20.0 + 40.0 * rand() / (double)RAND_MAX);
        float a = dm_logf(x), b = logf(x);
        if (a != b) {
            int32_t ia, ib;
            memcpy(&ia, &a, 4);
            memcpy(&ib, &b, 4);
            nd++;
            long u = labs((long)ia - (long)ib);
            if (u > worst_ulp) {
                worst_ulp = u;
            }
        }
    }
    const int ok = worst_ulp <= 2;
    fails += !ok;
    printf("%s logf     2000000 values: differ from libm %ld, worst %ld ulp\n", ok ? "PASS" : "FAIL", nd, worst_ulp);
    printf("%s: %d failed\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
