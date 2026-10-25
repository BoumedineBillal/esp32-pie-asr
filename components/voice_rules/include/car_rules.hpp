// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The car cabin demo's actions and the words of each: the climate (temperature, fan), four windows, two heated front
 * seats, the windscreen's defrost, the route, the media, the headlights, the range.
 */
#pragma once

#include "voice_rules.hpp"

namespace car {

/* the rules' order (voice::Action::rule) */
enum Rule {
    SET_TEMP, WARMER, COOLER, FAN_SET, FAN_UP, FAN_DOWN, WINDOWS_OPEN, WINDOWS_CLOSE, SEAT_ON, SEAT_OFF, DEFROST,
    NAVIGATE, STOP_NAV, PLAY, PAUSE, NEXT_TRACK, VOLUME_UP, VOLUME_DOWN, LIGHTS_ON, LIGHTS_OFF, RANGE, RULES
};

/* the windows (and the seats: DRIVER and PASSENGER) */
enum Window { DRIVER, PASSENGER, REAR_LEFT, REAR_RIGHT, WINDOWS };
/* place(): a side said */
enum Side { SIDE_NONE = -1, SIDE_DRIVER, SIDE_PASSENGER, SIDE_FRONT, SIDE_REAR, SIDE_ALL };
enum Destination { HOME, WORK, CHARGER, AIRPORT, STATION, BEACH, DESTINATIONS };

const voice::Demo &demo();

int side(const voice::Action &a);
int destination(const voice::Action &a);       /* NAVIGATE's target; -1 */
const char *media(const voice::Action &a);     /* PLAY's target as said */

const char *destination_name(int d);           /* "Home" */
const char *window_name(int w);                /* "driver" */

} // namespace car
