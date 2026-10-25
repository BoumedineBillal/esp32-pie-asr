// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// The decode kernels in C (the reference of pie/decode.S).
#include <string.h>
#include "kernels/kernels.h"

void pq_decode2(const uint8_t *codes, int nbytes, const uint32_t *lut, int8_t *out)
{
    for (int i = 0; i < nbytes; i++) {
        memcpy(out + 4 * i, &lut[codes[i]], 4);
    }
}

void pq_decode4(const uint8_t *codes, int nbytes, const uint32_t *lut, int8_t *out)
{
    for (int i = 0; i < nbytes; i++) {
        out[2 * i] = (int8_t)(lut[codes[i]] & 0xff);
        out[2 * i + 1] = (int8_t)((lut[codes[i]] >> 8) & 0xff);
    }
}
