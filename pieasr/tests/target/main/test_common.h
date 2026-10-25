// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// What the kernel tests share: the random numbers (one sequence over all the tests) and the report lines.
#pragma once
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

uint32_t test_rnd(void);

// one line on the console
static inline void report(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static inline uint32_t cycles_now(void)
{
    uint32_t c;
    __asm__ volatile("csrr %0, mcycle" : "=r"(c));
    return c;
}

void test_int_kernels(void);
