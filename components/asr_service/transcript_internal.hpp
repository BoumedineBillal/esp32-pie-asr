// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * What transcript.cpp and rewrite.cpp give each other.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include "transcript.hpp"

namespace transcript {
/* a final word into the pools (under the lock) */
void append_word(const char *txt, bool punct, int32_t end_ms, int32_t received_ms, bool cont = false);

/* s appended at dst[o] (cut at cap); the new length */
inline size_t append(char *dst, size_t cap, size_t o, const char *s)
{
    while (*s && o + 1 < cap)
        dst[o++] = *s++;
    dst[o] = 0;
    return o;
}
} // namespace transcript

/* the spoken forms -> shown forms (rewrite.cpp); all calls under the transcript's lock */
namespace rewrite {
void init();
void set(const transcript::Rewrite *r, int n);
bool active();
/* a closed word: it waits while it may start a spoken form, else goes out (append_word) */
void push(const char *txt, bool punct, int32_t end_ms, int32_t received_ms, bool cont = false);
/* the waiting words out: unless forced, they wait while a longer form may still complete */
void resolve(bool force, int32_t received_ms);
int pending();
void clear();
/* the waiting words appended to dst at o (the word being written); the new length */
size_t text(char *dst, size_t cap, size_t o);
} // namespace rewrite
