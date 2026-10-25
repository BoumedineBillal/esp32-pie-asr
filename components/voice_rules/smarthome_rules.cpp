// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * smarthome_rules.hpp. Choices that keep false actions away: no "warmer" or "it is cold" alone (they open ordinary
 * sentences); no "turn" alone as a verb (it would take "turn the lights off" from its particle); a room is a place,
 * never a target, so "the kitchen" alone does nothing.
 */
#include "smarthome_rules.hpp"
#include <cstring>

namespace smarthome {

namespace {

const char *const PLACES[] = {"kitchen", "living room", "livingroom", "lounge", "bedroom", "bathroom", "front",
                              "back",    "all",         "everywhere", "every room", "whole house", nullptr};
constexpr int PLACE_ROOM[] = {KITCHEN, LIVING_ROOM, LIVING_ROOM, LIVING_ROOM, BEDROOM, BATHROOM, -1,
                              -1,      ALL,         ALL,         ALL,         ALL};
constexpr int PLACE_DOOR[] = {-1, -1, -1, -1, -1, -1, FRONT, BACK, ALL, ALL, ALL, ALL};

const char *const LIGHTS[] = {"lights", "light", "lamp", "lamps", nullptr};
const char *const THERMO[] = {"thermostat", "temperature", "heating", "heat", nullptr};
const char *const DOOR_T[] = {"door", "doors", nullptr};
const char *const BLINDS[] = {"blinds", "blind", "curtains", "shades", "shutters", nullptr};
const char *const MUSIC[] = {"music", "some music", "jazz", "some jazz", "rock", "classical music", "the radio",
                             "radio", "my playlist", "a song", nullptr};
const char *const VOLUME[] = {"volume", "music", nullptr};
const char *const NONE[] = {nullptr};

const char *const ON_VERBS[] = {"turn on", "switch on", "put on", nullptr};
const char *const OFF_VERBS[] = {"turn off", "switch off", nullptr};
const char *const SET_VERBS[] = {"set", "dim", "change", nullptr};
const char *const DIM_VERBS[] = {"dim", "lower", nullptr};
const char *const BRIGHT_VERBS[] = {"brighten", "raise", nullptr};
const char *const TEMP_VERBS[] = {"set", "change", "put", nullptr};
const char *const UP_VERBS[] = {"turn up", "raise", "increase", nullptr};
const char *const DOWN_VERBS[] = {"turn down", "lower", "decrease", nullptr};
const char *const WARMER_ALONE[] = {"make it warmer", "make it a bit warmer", "a bit warmer", nullptr};
const char *const COOLER_ALONE[] = {"make it cooler", "make it colder", "make it a bit cooler", "a bit cooler",
                                    nullptr};
const char *const LOCK_VERBS[] = {"lock", nullptr};
const char *const UNLOCK_VERBS[] = {"unlock", "open", nullptr};
const char *const OPEN_VERBS[] = {"open", "raise", "pull up", "roll up", nullptr};
const char *const CLOSE_VERBS[] = {"close", "lower", "shut", "draw", "pull down", "roll down", nullptr};
const char *const PLAY_VERBS[] = {"play", "put on", "turn on", "start", nullptr};
const char *const STOP_VERBS[] = {"stop", "pause", "turn off", "switch off", "mute", nullptr};
const char *const NEXT_ALONE[] = {"next song", "next track", "skip this song", "skip the song", "skip song", nullptr};
const char *const STATUS_ALONE[] = {"what is the temperature", "is the front door locked", "is the back door locked",
                                    "are the doors locked", "is the door locked", "are the lights on",
                                    "what is playing", "house status", "status report", nullptr};

const voice::Rule TABLE[RULES] = {
    {"lights_on", "lights on", ON_VERBS, LIGHTS, NONE, voice::PARTICLE},
    {"lights_off", "lights off", OFF_VERBS, LIGHTS, NONE, voice::PARTICLE},
    {"set_lights", "set the lights", SET_VERBS, LIGHTS, NONE, voice::NUM_REQUIRED},
    {"dim_lights", "dim the lights", DIM_VERBS, LIGHTS, NONE, 0},
    {"brighten_lights", "brighten the lights", BRIGHT_VERBS, LIGHTS, NONE, 0},
    {"set_temperature", "set the temperature", TEMP_VERBS, THERMO, NONE, voice::NUM_REQUIRED},
    {"warmer", "warmer", UP_VERBS, THERMO, WARMER_ALONE, voice::PARTICLE | voice::WHOLE},
    {"cooler", "cooler", DOWN_VERBS, THERMO, COOLER_ALONE, voice::PARTICLE | voice::WHOLE},
    {"lock", "lock", LOCK_VERBS, DOOR_T, NONE, 0},
    {"unlock", "unlock", UNLOCK_VERBS, DOOR_T, NONE, 0},
    {"blinds_open", "open the blinds", OPEN_VERBS, BLINDS, NONE, voice::PARTICLE},
    {"blinds_close", "close the blinds", CLOSE_VERBS, BLINDS, NONE, voice::PARTICLE},
    {"play", "play", PLAY_VERBS, MUSIC, NONE, voice::PARTICLE},
    {"stop_music", "stop the music", STOP_VERBS, MUSIC, NONE, voice::PARTICLE},
    {"volume_up", "volume up", UP_VERBS, VOLUME, NONE, voice::PARTICLE},
    {"volume_down", "volume down", DOWN_VERBS, VOLUME, NONE, voice::PARTICLE},
    {"next_song", "next song", NONE, NONE, NEXT_ALONE, voice::WHOLE},
    {"status", "status", NONE, NONE, STATUS_ALONE, 0},
};

const voice::Demo DEMO = {"smarthome", "Smart home", TABLE, RULES, PLACES};

} // namespace

const voice::Demo &demo() { return DEMO; }

int room(const voice::Action &a) { return a.place >= 0 ? PLACE_ROOM[a.place] : -1; }

int door(const voice::Action &a) { return a.place >= 0 ? PLACE_DOOR[a.place] : -1; }

const char *media(const voice::Action &a)
{
    return a.rule == PLAY && a.target >= 0 ? MUSIC[a.target] : "music";
}

const char *room_name(int r)
{
    static const char *const N[ROOMS] = {"Kitchen", "Living room", "Bedroom", "Bathroom"};
    return r >= 0 && r < ROOMS ? N[r] : "";
}

const char *door_name(int d)
{
    static const char *const N[DOORS] = {"front", "back"};
    return d >= 0 && d < DOORS ? N[d] : "";
}

} // namespace smarthome
