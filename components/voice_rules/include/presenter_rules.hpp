// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The presenter demo's actions and the words of each: a deck of ten slides, the show started or not, a blanked
 * screen, a zoom, the talk's timer.
 */
#pragma once

#include "voice_rules.hpp"

namespace presenter {

/* the rules' order (voice::Action::rule) */
enum Rule {
    NEXT, PREVIOUS, GO_TO, FIRST, LAST, START, END, BLANK, UNBLANK, TIMER_START, TIMER_RESET, ZOOM_IN, ZOOM_OUT,
    TIME_LEFT, RULES
};

constexpr int SLIDES = 10;

const voice::Demo &demo();

const char *slide_title(int i);   /* slide i (0-based) */

} // namespace presenter
