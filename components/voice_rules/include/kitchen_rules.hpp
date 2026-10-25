// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The kitchen demo's actions and the words of each: a hob of four burners (front left, front right, back left, back
 * right; levels 1 to 9), an oven, up to three timers, a kettle, the extractor hood, the lights.
 */
#pragma once

#include "voice_rules.hpp"

namespace kitchen {

/* the rules' order (voice::Action::rule) */
enum Rule {
    BURNER_ON, BURNER_SET, BURNER_UP, BURNER_DOWN, BURNER_OFF, OVEN_ON, OVEN_OFF, TIMER_SET, TIMER_CANCEL, KETTLE,
    HOOD_ON, HOOD_OFF, LIGHTS_ON, LIGHTS_OFF, ALL_OFF, STATUS, RULES
};

enum Burner { FRONT_LEFT, FRONT_RIGHT, BACK_LEFT, BACK_RIGHT, BURNERS };
constexpr int ALL = -2;                 /* burner(): every burner */

const voice::Demo &demo();

/* a burner action's burner: one, ALL ("all the burners", "the burners"), or -1 (none said: the simulation picks) */
int burner(const voice::Action &a);
const char *burner_name(int b);         /* "front left" */

} // namespace kitchen
