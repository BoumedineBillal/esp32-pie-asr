// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The user's settings, kept in NVS across reboots (the Settings and Phrases screens change them).
 */
#pragma once

#include <cstdint>

namespace settings {

struct Settings {
    float gain_db = 54;          /* the microphone's: the fewest errors measured (p4mic's default: 42) */
    float word_timeout_s = 0.5f; /* a word is final after this long without a token */
    float pause_s = 1.5f;        /* a new line after a pause this long */
    int text_size = 1;           /* 0 small, 1 medium, 2 large */
    bool dark = true;
    bool show_tentative = true;  /* the word being written, in grey */
    bool timestamps = false;     /* each line's start time */
    bool follow = true;          /* the transcript follows the newest words */
    bool phrases_on = false;     /* the Phrases screen's switch for all of them */
    uint32_t phrases_off = 0;    /* bit i: the file's item i switched off */
};

void load();
void save();
Settings &get();

} // namespace settings
