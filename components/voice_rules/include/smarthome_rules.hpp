// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The smart home demo's actions and the words of each: four rooms (kitchen, living room, bedroom, bathroom) with a
 * light and blinds each, a thermostat, a front and a back door, music in a room.
 */
#pragma once

#include "voice_rules.hpp"

namespace smarthome {

/* the rules' order (voice::Action::rule) */
enum Rule {
    LIGHTS_ON, LIGHTS_OFF, SET_LIGHTS, DIM_LIGHTS, BRIGHTEN_LIGHTS, SET_TEMP, WARMER, COOLER, LOCK, UNLOCK,
    BLINDS_OPEN, BLINDS_CLOSE, PLAY, STOP_MUSIC, VOLUME_UP, VOLUME_DOWN, NEXT_SONG, STATUS, RULES
};

enum Room { KITCHEN, LIVING_ROOM, BEDROOM, BATHROOM, ROOMS };
enum Door { FRONT, BACK, DOORS };
constexpr int ALL = -2;          /* place(): every room or both doors */

const voice::Demo &demo();

/* an action's place as the simulation's: a room (lights, blinds, music), a door, ALL, or -1 (none said) */
int room(const voice::Action &a);
int door(const voice::Action &a);
/* what PLAY plays: "jazz", "the radio", "music" ... (the target as said) */
const char *media(const voice::Action &a);

const char *room_name(int r);    /* "Kitchen" */
const char *door_name(int d);    /* "front" */

} // namespace smarthome
