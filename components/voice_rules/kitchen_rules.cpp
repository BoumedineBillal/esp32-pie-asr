// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * kitchen_rules.hpp. A burner is said by its place ("the front left burner", "the back right one" is not taken);
 * "put the kettle on" is the particle of "put on"; the timers need their minutes ("set a timer" alone does nothing).
 */
#include "kitchen_rules.hpp"
#include <cstring>

namespace kitchen {

namespace {

/* (the recognizer may join them: "backright") */
const char *const PLACES[] = {"front left", "front right", "back left", "back right", "left front", "right front",
                              "left back",  "right back",  "frontleft", "frontright", "backleft",  "backright",
                              "all",        nullptr};
constexpr int PLACE_BURNER[] = {FRONT_LEFT, FRONT_RIGHT, BACK_LEFT,  BACK_RIGHT, FRONT_LEFT, FRONT_RIGHT, BACK_LEFT,
                                BACK_RIGHT, FRONT_LEFT,  FRONT_RIGHT, BACK_LEFT, BACK_RIGHT, ALL};

const char *const BURNER_T[] = {"burner", "burners", "hob", "ring", "rings", "stove", "cooktop", "hotplate",
                                "hot plate", nullptr};
const char *const OVEN_T[] = {"oven", nullptr};
const char *const TIMER_T[] = {"timer", "timers", "a timer", "minute timer", nullptr};
const char *const KETTLE_T[] = {"kettle", "water", "some water", nullptr};
const char *const HOOD_T[] = {"extractor fan", "extractor", "fan", "hood", "cooker hood", "extractor hood", "vent",
                              nullptr};
const char *const LIGHTS_T[] = {"lights", "light", nullptr};
const char *const NONE[] = {nullptr};

const char *const ON_VERBS[] = {"turn on", "switch on", nullptr};
const char *const SET_VERBS[] = {"set", "put", "turn", nullptr};
const char *const UP_VERBS[] = {"turn up", "raise", "increase", nullptr};
const char *const DOWN_VERBS[] = {"turn down", "lower", "reduce", nullptr};
const char *const OFF_VERBS[] = {"turn off", "switch off", nullptr};
const char *const OVEN_VERBS[] = {"preheat", "heat", "heat up", "turn on", "switch on", "set", "start", nullptr};
const char *const TIMER_VERBS[] = {"set", "start", nullptr};
const char *const CANCEL_VERBS[] = {"cancel", "stop", "clear", "turn off", "delete", nullptr};
const char *const KETTLE_VERBS[] = {"boil", "turn on", "switch on", "put on", "start", nullptr};
const char *const HOOD_ON_VERBS[] = {"turn on", "switch on", "start", nullptr};
const char *const HOOD_OFF_VERBS[] = {"turn off", "switch off", "stop", nullptr};
const char *const ALL_OFF_ALONE[] = {"turn everything off", "turn off everything", "switch everything off",
                                     "switch off everything", "everything off", nullptr};
const char *const STATUS_ALONE[] = {"how much time is left", "how long is left", "how much time left",
                                    "is the oven ready",     "is the oven hot",  "what is the oven temperature",
                                    "kitchen status",        "status report",    nullptr};

const voice::Rule TABLE[RULES] = {
    {"burner_on", "burner on", ON_VERBS, BURNER_T, NONE, voice::NUM_OPTIONAL | voice::PARTICLE},
    {"burner_set", "set a burner", SET_VERBS, BURNER_T, NONE, voice::NUM_REQUIRED},
    {"burner_up", "burner up", UP_VERBS, BURNER_T, NONE, voice::PARTICLE},
    {"burner_down", "burner down", DOWN_VERBS, BURNER_T, NONE, voice::PARTICLE},
    {"burner_off", "burner off", OFF_VERBS, BURNER_T, NONE, voice::PARTICLE},
    {"oven_on", "oven on", OVEN_VERBS, OVEN_T, NONE, voice::NUM_OPTIONAL | voice::PARTICLE},
    {"oven_off", "oven off", OFF_VERBS, OVEN_T, NONE, voice::PARTICLE},
    {"timer_set", "set a timer", TIMER_VERBS, TIMER_T, NONE, voice::NUM_REQUIRED},
    {"timer_cancel", "cancel the timer", CANCEL_VERBS, TIMER_T, NONE, voice::PARTICLE},
    {"kettle", "boil the kettle", KETTLE_VERBS, KETTLE_T, NONE, voice::PARTICLE},
    {"hood_on", "hood on", HOOD_ON_VERBS, HOOD_T, NONE, voice::PARTICLE},
    {"hood_off", "hood off", HOOD_OFF_VERBS, HOOD_T, NONE, voice::PARTICLE},
    {"lights_on", "lights on", ON_VERBS, LIGHTS_T, NONE, voice::PARTICLE},
    {"lights_off", "lights off", OFF_VERBS, LIGHTS_T, NONE, voice::PARTICLE},
    {"all_off", "everything off", NONE, NONE, ALL_OFF_ALONE, voice::WHOLE},
    {"status", "status", NONE, NONE, STATUS_ALONE, 0},
};

const voice::Demo DEMO = {"kitchen", "Kitchen", TABLE, RULES, PLACES};

} // namespace

const voice::Demo &demo() { return DEMO; }

int burner(const voice::Action &a) { return a.place >= 0 ? PLACE_BURNER[a.place] : -1; }

const char *burner_name(int b)
{
    static const char *const N[BURNERS] = {"front left", "front right", "back left", "back right"};
    return b >= 0 && b < BURNERS ? N[b] : "";
}

} // namespace kitchen
