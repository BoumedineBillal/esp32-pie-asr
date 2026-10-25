// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The phrases: the phrase file's words and sentences (pieasr/phrases.h's format: "shown" or "shown = spoken |
 * spoken", '#' notes). While they are on, the beam search gives the lines that are on a bonus per token, and the
 * transcript writes their shown form.
 *
 * At most COLUMN words and COLUMN sentences are used, in the file's order; each one is switched on or off, and one
 * switch turns them all on or off. A change is applied by the main task between chunks.
 *
 * A firmware may give more lists (asr::Config::more_phrase_files, a demo's own words): select() picks the list in
 * use, applied between chunks like a switch; every function below is about the list in use. Each list keeps its own
 * switches; lists after the first start on, every line on. The lists are read once, at boot, and stay in memory.
 */
#pragma once

#include <cstdint>
#include "pieasr/phrases.h"

namespace phrases {

constexpr int COLUMN = 10;     /* words, and sentences */
constexpr float BONUS = 2.0f;  /* per token: stronger ones begin to invent phrases */

int words();                   /* the words used */
int sentences();
int word(int j);               /* an item's index */
int sentence(int j);
const pieasr_phrase_t &item(int i);
int ignored();                 /* lines beyond the columns or not understood */
const char *source();          /* where the list came from */

/* the switches, kept by the app (its settings); they take effect between chunks */
bool on();
bool enabled(int i);
void set_on(bool on);
void set_enabled(int i, bool on);
/* the app's stored switches: on, and the items switched off (bit i: item i) */
void set_state(bool on, uint32_t off_mask);
uint32_t off_mask();

/* the lists: 1 + the more_phrase_files configured; the one in use (0 at boot) */
constexpr int LISTS = 8;
int lists();
int selected();
void select(int list);

} // namespace phrases
