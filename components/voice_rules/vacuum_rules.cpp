// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * vacuum_rules.hpp. Choices that keep false actions away: "dock" is only a target (a misheard "dog" alone does
 * nothing); "go back" is a verb of both moves, so "go back to the kitchen" goes there and "go back to the dock"
 * returns, but "go back" alone does nothing ("I want to go back to sleep"); nothing acts on a room or a mess without
 * a verb.
 */
#include "vacuum_rules.hpp"
#include <cstring>

namespace vacuum {

namespace {

const char *const CLEAN[] = {"clean", "clean up", "vacuum", "hoover", nullptr};
const char *const SPOT_VERBS[] = {"clean", "clean up", "vacuum", "hoover", "pick up", "suck up", "get", nullptr};
const char *const SPOT_TARGETS[] = {"spill", "granola", "crumbs", "crumb", "dust", nullptr};
const char *const ROOM_TARGETS[] = {"kitchen", "living room", "livingroom", "lounge", "bedroom", nullptr};
const char *const ALL_TARGETS[] = {"everything", "everywhere", "whole flat", "whole house", "whole apartment",
                                   "all rooms", "all the rooms", "every room", "flat", "house", "apartment", nullptr};
const char *const ALL_ALONE[] = {"start cleaning", nullptr};
const char *const GO_VERBS[] = {"go to", "go back", "go into", "drive to", "move to", "head to", nullptr};
const char *const DOCK_VERBS[] = {"go back", "go to", "return to", "drive to", "head back to", "head to", "back to",
                                  nullptr};
const char *const DOCK_TARGETS[] = {"dock", "charger", "base", "charging station", nullptr};
const char *const DOCK_ALONE[] = {"go home", "come home", "return home", "go charge", "recharge", nullptr};
const char *const STOP_ALONE[] = {"stop", "pause", "halt", "hold on", "freeze", nullptr};
const char *const RESUME_ALONE[] = {"resume", "continue", "carry on", "keep going", "go on", "start again", nullptr};
const char *const SUCTION_VERBS[] = {"suction", "suction power", "power", "set the suction to", "set suction to",
                                     "set the suction", "turn the suction to", "turn the suction", "put the suction on",
                                     "set the power to", nullptr};
const char *const SUCTION_TARGETS[] = {"low", "medium", "high", "max", "maximum", "quiet", "turbo", nullptr};
const char *const STATUS_ALONE[] = {"where are you", "what are you doing", "how much battery", "battery",
                                    "status", nullptr};
const char *const NONE[] = {nullptr};

const voice::Rule TABLE[RULES] = {
    {"clean_spot", "clean spot", SPOT_VERBS, SPOT_TARGETS, NONE},
    {"clean_room", "clean room", CLEAN, ROOM_TARGETS, NONE},
    {"clean_all", "clean everything", CLEAN, ALL_TARGETS, ALL_ALONE},
    {"go_to_room", "go to room", GO_VERBS, ROOM_TARGETS, NONE},
    {"return_to_dock", "return to dock", DOCK_VERBS, DOCK_TARGETS, DOCK_ALONE},
    {"stop", "stop", NONE, NONE, STOP_ALONE},
    {"resume", "resume", NONE, NONE, RESUME_ALONE},
    {"suction", "suction", SUCTION_VERBS, SUCTION_TARGETS, NONE},
    {"status", "status", NONE, NONE, STATUS_ALONE},
};

const voice::Demo DEMO = {"vacuum", "Robot vacuum", TABLE, RULES};

} // namespace

const voice::Demo &demo() { return DEMO; }

int room(const voice::Action &a)
{
    if ((a.rule != CLEAN_ROOM && a.rule != GO_TO_ROOM) || a.target < 0)
        return -1;
    const char *t = ROOM_TARGETS[a.target];
    if (!strcmp(t, "kitchen"))
        return KITCHEN;
    if (!strcmp(t, "bedroom"))
        return BEDROOM;
    return LIVING_ROOM; /* living room, livingroom, lounge */
}

int spot(const voice::Action &a)
{
    if (a.rule != CLEAN_SPOT || a.target < 0)
        return -1;
    const char *t = SPOT_TARGETS[a.target];
    if (!strcmp(t, "spill"))
        return SPILL;
    if (!strcmp(t, "granola"))
        return GRANOLA;
    if (!strcmp(t, "dust"))
        return DUST;
    return CRUMBS; /* crumbs, crumb */
}

int suction(const voice::Action &a)
{
    if (a.rule != SUCTION || a.target < 0)
        return -1;
    const char *t = SUCTION_TARGETS[a.target];
    if (!strcmp(t, "low") || !strcmp(t, "quiet"))
        return LOW;
    if (!strcmp(t, "medium"))
        return MEDIUM;
    if (!strcmp(t, "high"))
        return HIGH;
    return MAX; /* max, maximum, turbo */
}

const char *room_name(int r)
{
    static const char *const N[ROOMS] = {"Kitchen", "Living room", "Bedroom"};
    return r >= 0 && r < ROOMS ? N[r] : "";
}

const char *spot_name(int s)
{
    static const char *const N[SPOTS] = {"spill", "granola", "crumbs", "dust"};
    return s >= 0 && s < SPOTS ? N[s] : "";
}

const char *suction_name(int s)
{
    static const char *const N[SUCTIONS] = {"low", "medium", "high", "max"};
    return s >= 0 && s < SUCTIONS ? N[s] : "";
}

} // namespace vacuum
