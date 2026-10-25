// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * smarthome_sim.hpp. A light, blinds or music said without a room: every room for the lights and the blinds ("turn
 * off the lights"), the living room for the music; "the door" without a side: the front one, "the doors": both.
 */
#include "smarthome_sim.hpp"
#include <cmath>
#include <cstring>

namespace smarthome {

namespace {

constexpr float BLINDS_SPEED = 0.5f;   /* of the way a second */
constexpr float TEMP_SPEED = 0.1f;     /* degrees a second */
const char *const SONGS[] = {"Blue in Green", "So What", "Autumn Leaves", "Take Five", "Naima"};
constexpr int NSONGS = 5;

const char *target_text(const voice::Action &a)
{
    const voice::Rule &r = demo().rules[a.rule];
    return a.target >= 0 ? r.targets[a.target] : "";
}

} // namespace

void Home::reset()
{
    for (int r = 0; r < ROOMS; r++) {
        light_[r] = 0;
        last_level_[r] = 100;
        blinds_[r] = blinds_to_[r] = 1.0f;
    }
    light_[LIVING_ROOM] = 60;
    locked_[FRONT] = true;
    locked_[BACK] = false;
    temp_ = shown_temp_ = 19.5f;
    target_ = 21;
    playing_ = false;
    music_room_ = LIVING_ROOM;
    song_ = 0;
    volume_ = 4;
    snprintf(media_, sizeof media_, "jazz");
    clear();
}

/* the rooms an action names: one, every room (ALL, or none said when all_by_default) */
void Home::rooms_of(const voice::Action &a, bool all_by_default, bool *out) const
{
    const int r = room(a);
    for (int k = 0; k < ROOMS; k++)
        out[k] = r == ALL || (r < 0 && all_by_default) || r == k;
}

void Home::doors_of(const voice::Action &a, bool *out) const
{
    const int d = door(a);
    const bool both = d == ALL || (d < 0 && !strcmp(target_text(a), "doors"));
    for (int k = 0; k < DOORS; k++)
        out[k] = both || d == k || (d < 0 && !both && k == FRONT);
}

void Home::order(const voice::Result &r)
{
    begin(r);
    for (int i = 0; i < r.n; i++) {
        const voice::Action &a = r.actions[i];
        if (a.skipped)
            continue;
        bool rooms[ROOMS], doors[DOORS];
        switch (a.rule) {
        case LIGHTS_ON:
            rooms_of(a, true, rooms);
            for (int k = 0; k < ROOMS; k++)
                if (rooms[k] && !light_[k])
                    light_[k] = last_level_[k];
            break;
        case LIGHTS_OFF:
            rooms_of(a, true, rooms);
            for (int k = 0; k < ROOMS; k++)
                if (rooms[k] && light_[k]) {
                    last_level_[k] = light_[k];
                    light_[k] = 0;
                }
            break;
        case SET_LIGHTS:
        case DIM_LIGHTS:
        case BRIGHTEN_LIGHTS:
            rooms_of(a, true, rooms);
            for (int k = 0; k < ROOMS; k++) {
                if (!rooms[k])
                    continue;
                int v = light_[k];
                if (a.rule == SET_LIGHTS)
                    v = a.value;
                else if (a.rule == DIM_LIGHTS) /* (dim, never off) */
                    v = v ? (v - 30 < 10 ? 10 : v - 30) : 30;
                else
                    v = v ? v + 30 : 70;
                light_[k] = v < 0 ? 0 : v > 100 ? 100 : v;
                if (light_[k])
                    last_level_[k] = light_[k];
            }
            break;
        case SET_TEMP:
            target_ = a.value < 10 ? 10 : a.value > 30 ? 30 : a.value;
            if (target_ != a.value)
                say("The thermostat goes from 10 to 30 degrees: set to %d.", target_);
            break;
        case WARMER:
            target_ = target_ < 30 ? target_ + 1 : 30;
            break;
        case COOLER:
            target_ = target_ > 10 ? target_ - 1 : 10;
            break;
        case LOCK:
        case UNLOCK:
            doors_of(a, doors);
            for (int k = 0; k < DOORS; k++)
                if (doors[k])
                    locked_[k] = a.rule == LOCK;
            break;
        case BLINDS_OPEN:
        case BLINDS_CLOSE:
            rooms_of(a, true, rooms);
            for (int k = 0; k < ROOMS; k++)
                if (rooms[k])
                    blinds_to_[k] = a.rule == BLINDS_OPEN ? 1.0f : 0.0f;
            act_[i] = RUNNING;
            break;
        case PLAY: {
            const int rm = room(a);
            music_room_ = rm >= 0 ? rm : LIVING_ROOM;
            const char *m = smarthome::media(a);
            if (!strncmp(m, "some ", 5))
                m += 5;
            if (!strncmp(m, "the ", 4))
                m += 4;
            snprintf(media_, sizeof media_, "%s", m);
            if (playing_)
                song_ = (song_ + 1) % NSONGS;
            playing_ = true;
            break;
        }
        case STOP_MUSIC:
            playing_ = false;
            break;
        case VOLUME_UP:
            volume_ = volume_ + 2 > 10 ? 10 : volume_ + 2;
            break;
        case VOLUME_DOWN:
            volume_ = volume_ - 2 < 0 ? 0 : volume_ - 2;
            break;
        case NEXT_SONG:
            song_ = (song_ + 1) % NSONGS;
            playing_ = true;
            break;
        case STATUS: {
            char lights[80] = "";
            int n = 0, len = 0;
            for (int k = 0; k < ROOMS; k++)
                if (light_[k])
                    len += snprintf(lights + len, sizeof lights - len, "%s%s", n++ ? ", " : "", room_name(k));
            say("%.1f degrees inside, the thermostat at %d. Front door %s, back door %s. Lights on: %s.%s%s",
                static_cast<double>(shown_temp_), target_, locked_[FRONT] ? "locked" : "unlocked",
                locked_[BACK] ? "locked" : "unlocked", n ? lights : "none", playing_ ? " Playing " : "",
                playing_ ? media_ : "");
            act_[i] = ANSWERED;
            break;
        }
        default: break;
        }
    }
}

void Home::update(float dt)
{
    if (dt <= 0)
        return;
    bool moving = false;
    for (int k = 0; k < ROOMS; k++) {
        if (blinds_[k] == blinds_to_[k])
            continue;
        const float before = std::floor(blinds_[k] * 4);
        const float step = BLINDS_SPEED * dt;
        blinds_[k] = blinds_[k] < blinds_to_[k] ? std::fmin(blinds_to_[k], blinds_[k] + step)
                                                : std::fmax(blinds_to_[k], blinds_[k] - step);
        if (std::floor(blinds_[k] * 4) != before || blinds_[k] == blinds_to_[k])
            version_++;
        moving = moving || blinds_[k] != blinds_to_[k];
    }
    if (!moving)
        for (int i = 0; i < nact_; i++)
            if (act_[i] == RUNNING) {
                act_[i] = DONE;
                version_++;
            }
    const float d = target_ - temp_;
    temp_ += std::fabs(d) < TEMP_SPEED * dt ? d : (d > 0 ? 1 : -1) * TEMP_SPEED * dt;
    const float shown = std::round(temp_ * 10) / 10;
    if (shown != shown_temp_) {
        shown_temp_ = shown;
        version_++;
    }
}

const char *Home::song() const { return SONGS[song_]; }

const char *Home::status(char *out, int cap) const
{
    int on = 0;
    for (int k = 0; k < ROOMS; k++)
        on += light_[k] > 0;
    snprintf(out, cap, "%.1f degrees inside (thermostat %d), lights on in %d of 4 rooms, front door %s%s%s",
             static_cast<double>(shown_temp_), target_, on, locked_[FRONT] ? "locked" : "unlocked",
             playing_ ? ", playing " : "", playing_ ? media_ : "");
    return out;
}

} // namespace smarthome
