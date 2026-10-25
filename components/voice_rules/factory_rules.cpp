// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * factory_rules.hpp. The emergency stop comes first (a rule's order decides between phrases of the same words) and
 * fires as a whole part only ("stop everything you are doing and listen" is not one); no "emergency" or "faster"
 * alone (they open ordinary sentences).
 */
#include "factory_rules.hpp"
#include <cstring>

namespace factory {

namespace {

const char *const MACHINES_T[] = {"line",  "the line",  "conveyor", "conveyor belt", "belt", "production",
                                  "production line", "station", "press", "the press", "robot arm", "robot",
                                  "arm",   "packer", "packing station", nullptr};
constexpr int MACHINE_OF[] = {LINE, LINE, LINE, LINE, LINE, LINE, LINE, -1, PRESS, PRESS, ARM, ARM, ARM, PACKER,
                              PACKER};
const char *const LINE_T[] = {"line", "the line", "conveyor", "conveyor belt", "belt", "production",
                              "production line", nullptr};
const char *const ALARM_T[] = {"alarm",          "alarms", "the alarm", "fault", "faults", "error", "errors",
                               "emergency stop", "e stop", "jam",       "the jam", nullptr};
const char *const SPEED_T[] = {"speed", "line speed", "conveyor speed", "belt speed", nullptr};
const char *const PRODUCT_T[] = {"red boxes", "blue boxes", "green boxes", "red", "blue", "green", "product a",
                                 "product b", "product c", nullptr};
constexpr int PRODUCT_OF[] = {RED, BLUE, GREEN, RED, BLUE, GREEN, RED, BLUE, GREEN};
const char *const NONE[] = {nullptr};

const char *const ESTOP_ALONE[] = {"emergency stop", "e stop", "stop everything", "stop all machines",
                                   "stop all the machines", "stop all", nullptr};
const char *const START_VERBS[] = {"start", "run", "restart", "resume", "turn on", "switch on", "start up", nullptr};
const char *const STOP_VERBS[] = {"stop", "halt", "pause", "turn off", "switch off", "shut down", nullptr};
const char *const RESET_VERBS[] = {"reset", "clear", "acknowledge", nullptr};
const char *const SET_VERBS[] = {"set", "change", "put", nullptr};
const char *const FASTER_VERBS[] = {"speed up", nullptr};
const char *const FASTER_ALONE[] = {"go faster", "speed up", "a bit faster", nullptr};
const char *const SLOWER_VERBS[] = {"slow down", nullptr};
const char *const SLOWER_ALONE[] = {"go slower", "slow down", "a bit slower", nullptr};
const char *const PRODUCT_VERBS[] = {"switch to", "change to", "make", "run", nullptr};
const char *const STATUS_ALONE[] = {"how many boxes", "how many parts", "how many units", "production count",
                                    "line status", "status report", "what is the speed", "any alarms",
                                    "are there any alarms", "what is the status", nullptr};

const voice::Rule TABLE[RULES] = {
    {"emergency_stop", "emergency stop", NONE, NONE, ESTOP_ALONE, voice::WHOLE},
    {"start", "start", START_VERBS, MACHINES_T, NONE, voice::NUM_OPTIONAL | voice::PARTICLE},
    {"stop", "stop", STOP_VERBS, MACHINES_T, NONE, voice::NUM_OPTIONAL | voice::PARTICLE},
    {"reset", "reset the alarm", RESET_VERBS, ALARM_T, NONE, 0},
    {"set_speed", "set the speed", SET_VERBS, SPEED_T, NONE, voice::NUM_REQUIRED},
    {"faster", "faster", FASTER_VERBS, LINE_T, FASTER_ALONE, voice::PARTICLE | voice::WHOLE},
    {"slower", "slower", SLOWER_VERBS, LINE_T, SLOWER_ALONE, voice::PARTICLE | voice::WHOLE},
    {"product", "switch product", PRODUCT_VERBS, PRODUCT_T, NONE, 0},
    {"status", "status", NONE, NONE, STATUS_ALONE, 0},
};

const voice::Demo DEMO = {"factory", "Factory line", TABLE, RULES};

} // namespace

const voice::Demo &demo() { return DEMO; }

int machine(const voice::Action &a)
{
    if ((a.rule != START && a.rule != STOP) || a.target < 0)
        return -1;
    const int m = MACHINE_OF[a.target];
    if (m >= 0)
        return m;
    return a.value >= 1 && a.value <= 3 ? a.value : -1; /* "station <n>": 1 the press, 2 the arm, 3 the packer */
}

int product(const voice::Action &a) { return a.rule == PRODUCT && a.target >= 0 ? PRODUCT_OF[a.target] : -1; }

const char *machine_name(int m)
{
    static const char *const N[MACHINES] = {"Line", "Press", "Robot arm", "Packer"};
    return m >= 0 && m < MACHINES ? N[m] : "";
}

const char *product_name(int p)
{
    static const char *const N[PRODUCTS] = {"red", "blue", "green"};
    return p >= 0 && p < PRODUCTS ? N[p] : "";
}

} // namespace factory
