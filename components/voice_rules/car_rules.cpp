// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * car_rules.hpp. "windows down" / "windows up" are the particles of "roll down" / "roll up"; "turn up" is the
 * climate's, the fan's or the volume's by its target; no "defrost" alone ("defrost the chicken").
 */
#include "car_rules.hpp"
#include <cstring>

namespace car {

namespace {

const char *const PLACES[] = {"driver", "drivers", "passenger", "passengers", "front", "rear", "back", "all",
                              nullptr};
constexpr int PLACE_SIDE[] = {SIDE_DRIVER, SIDE_DRIVER, SIDE_PASSENGER, SIDE_PASSENGER, SIDE_FRONT,
                              SIDE_REAR,   SIDE_REAR,   SIDE_ALL};

/* ("the heat": "heat" alone is the seats' verb, "heat my seat") */
const char *const TEMP[] = {"temperature", "cabin", "climate", "heating", "the heat", "side", "air conditioning",
                            nullptr};
const char *const HEAT[] = {"heating", "the heat", "temperature", nullptr};
const char *const FAN[] = {"fan", "fan speed", "blower", "air", nullptr};
const char *const WINDOW_T[] = {"window", "windows", nullptr};
const char *const SEAT[] = {"seat heater", "seat heaters", "seat heating", "seat", "seats", nullptr};
const char *const GLASS[] = {"windscreen", "windshield", "windows", "window", nullptr};
const char *const DEST[] = {"home",    "work",          "the office",  "office",    "nearest charger", "a charger",
                            "charger", "charging station", "the airport", "airport", "the station",
                            "train station", "station", "the beach", "beach", nullptr};
constexpr int DEST_OF[] = {HOME,    WORK,    WORK,    WORK,    CHARGER, CHARGER, CHARGER, CHARGER,
                           AIRPORT, AIRPORT, STATION, STATION, STATION, BEACH,   BEACH};
const char *const NAV_T[] = {"navigation", "route", "directions", nullptr};
const char *const MEDIA[] = {"music", "some music", "the radio", "radio", "a podcast", "podcast", "jazz",
                             "my playlist", "playlist", "the news", nullptr};
const char *const VOLUME[] = {"volume", "music", "radio", "sound", nullptr};
const char *const HEADLIGHTS[] = {"headlights", "lights", "fog lights", "high beams", "headlight", nullptr};
const char *const NONE[] = {nullptr};

const char *const SET_VERBS[] = {"set", "change", "put", nullptr};
const char *const UP_VERBS[] = {"turn up", "raise", "increase", nullptr};
const char *const DOWN_VERBS[] = {"turn down", "lower", "decrease", nullptr};
const char *const WARMER_ALONE[] = {"make it warmer", "make it a bit warmer", "a bit warmer", nullptr};
const char *const COOLER_ALONE[] = {"make it cooler", "make it colder", "make it a bit cooler", "a bit cooler",
                                    nullptr};
const char *const OPEN_VERBS[] = {"open", "roll down", "lower", "put down", "wind down", nullptr};
const char *const CLOSE_VERBS[] = {"close", "roll up", "raise", "put up", "shut", "wind up", nullptr};
const char *const ON_VERBS[] = {"turn on", "switch on", nullptr};
const char *const SEAT_ON_VERBS[] = {"turn on", "switch on", "heat", "warm up", nullptr};
const char *const OFF_VERBS[] = {"turn off", "switch off", nullptr};
const char *const DEFROST_VERBS[] = {"defrost", "demist", "clear", nullptr};
const char *const NAV_VERBS[] = {"navigate to", "navigate", "take me to", "take me", "drive to", "drive me to",
                                 "go to", "directions to", "get me to", "route to", nullptr};
const char *const STOP_NAV_VERBS[] = {"cancel", "stop", "end", nullptr};
const char *const STOP_NAV_ALONE[] = {"stop navigation", "cancel navigation", "cancel the route", "end navigation",
                                      nullptr};
const char *const PLAY_VERBS[] = {"play", "put on", "turn on", "start", nullptr};
const char *const PAUSE_VERBS[] = {"pause", "stop", "turn off", "switch off", "mute", nullptr};
const char *const NEXT_ALONE[] = {"next song", "next track", "skip this song", "skip the song", "skip song", nullptr};
const char *const RANGE_ALONE[] = {"how much range", "how much charge", "what is the range", "what is my range",
                                   "how much battery", "how far can i go", "car status", "status report", nullptr};

const voice::Rule TABLE[RULES] = {
    {"set_temperature", "set the temperature", SET_VERBS, TEMP, NONE, voice::NUM_REQUIRED},
    {"warmer", "warmer", UP_VERBS, HEAT, WARMER_ALONE, voice::PARTICLE | voice::WHOLE},
    {"cooler", "cooler", DOWN_VERBS, HEAT, COOLER_ALONE, voice::PARTICLE | voice::WHOLE},
    {"set_fan", "set the fan", SET_VERBS, FAN, NONE, voice::NUM_REQUIRED},
    {"fan_up", "fan up", UP_VERBS, FAN, NONE, voice::PARTICLE},
    {"fan_down", "fan down", DOWN_VERBS, FAN, NONE, voice::PARTICLE},
    {"windows_open", "open the windows", OPEN_VERBS, WINDOW_T, NONE, voice::PARTICLE},
    {"windows_close", "close the windows", CLOSE_VERBS, WINDOW_T, NONE, voice::PARTICLE},
    {"seat_heat_on", "seat heater on", SEAT_ON_VERBS, SEAT, NONE, voice::PARTICLE},
    {"seat_heat_off", "seat heater off", OFF_VERBS, SEAT, NONE, voice::PARTICLE},
    {"defrost", "defrost", DEFROST_VERBS, GLASS, NONE, 0},
    {"navigate", "navigate", NAV_VERBS, DEST, NONE, 0},
    {"stop_navigation", "stop navigation", STOP_NAV_VERBS, NAV_T, STOP_NAV_ALONE, voice::WHOLE},
    {"play", "play", PLAY_VERBS, MEDIA, NONE, voice::PARTICLE},
    {"pause", "pause", PAUSE_VERBS, MEDIA, NONE, voice::PARTICLE},
    {"next_track", "next track", NONE, NONE, NEXT_ALONE, voice::WHOLE},
    {"volume_up", "volume up", UP_VERBS, VOLUME, NONE, voice::PARTICLE},
    {"volume_down", "volume down", DOWN_VERBS, VOLUME, NONE, voice::PARTICLE},
    {"lights_on", "headlights on", ON_VERBS, HEADLIGHTS, NONE, voice::PARTICLE},
    {"lights_off", "headlights off", OFF_VERBS, HEADLIGHTS, NONE, voice::PARTICLE},
    {"range", "range", NONE, NONE, RANGE_ALONE, 0},
};

const voice::Demo DEMO = {"car", "Car cabin", TABLE, RULES, PLACES};

} // namespace

const voice::Demo &demo() { return DEMO; }

int side(const voice::Action &a) { return a.place >= 0 ? PLACE_SIDE[a.place] : SIDE_NONE; }

int destination(const voice::Action &a) { return a.rule == NAVIGATE && a.target >= 0 ? DEST_OF[a.target] : -1; }

const char *media(const voice::Action &a) { return a.rule == PLAY && a.target >= 0 ? MEDIA[a.target] : "music"; }

const char *destination_name(int d)
{
    static const char *const N[DESTINATIONS] = {"Home", "Work", "Nearest charger", "Airport", "Train station",
                                                "Beach"};
    return d >= 0 && d < DESTINATIONS ? N[d] : "";
}

const char *window_name(int w)
{
    static const char *const N[WINDOWS] = {"driver", "passenger", "rear left", "rear right"};
    return w >= 0 && w < WINDOWS ? N[w] : "";
}

} // namespace car
