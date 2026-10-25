// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The robot vacuum demo's actions and the words of each: a flat of three rooms (kitchen, living room, bedroom), four
 * messes (spill and granola in the kitchen, crumbs in the living room, dust in the bedroom), a dock in the kitchen.
 */
#pragma once

#include "voice_rules.hpp"

namespace vacuum {

/* the rules' order (voice::Action::rule) */
enum Rule { CLEAN_SPOT, CLEAN_ROOM, CLEAN_ALL, GO_TO_ROOM, RETURN_TO_DOCK, STOP, RESUME, SUCTION, STATUS, RULES };

enum Room { KITCHEN, LIVING_ROOM, BEDROOM, ROOMS };
enum Spot { SPILL, GRANOLA, CRUMBS, DUST, SPOTS };
enum Suction { LOW, MEDIUM, HIGH, MAX, SUCTIONS };

const voice::Demo &demo();

/* an action's target as the simulation's names: -1 if it is not one */
int room(const voice::Action &a);
int spot(const voice::Action &a);
int suction(const voice::Action &a);

const char *room_name(int r);       /* "Kitchen" */
const char *spot_name(int s);       /* "spill" */
const char *suction_name(int s);    /* "high" */

} // namespace vacuum
