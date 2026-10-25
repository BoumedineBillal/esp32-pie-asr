// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The smart home, simulated: a light (0-100 %) and blinds in each of four rooms, a thermostat the inside temperature
 * follows (0.1 degree a second), two doors, music playing in a room. A light, a door or the music change at once;
 * blinds take 2 s; the temperature moves slowly. Deterministic: update(dt) only.
 */
#pragma once

#include "smarthome_rules.hpp"
#include "world_base.hpp"

namespace smarthome {

class Home : public voice::BasicWorld {
public:
    Home() { reset(); }
    void reset() override;
    void order(const voice::Result &r) override;
    void update(float dt) override;
    const char *status(char *out, int cap) const override;

    int light(int room) const { return light_[room]; }          /* 0: off, else its level % */
    float blinds(int room) const { return blinds_[room]; }      /* 0 closed .. 1 open (version(): each quarter) */
    bool locked(int door) const { return locked_[door]; }
    float inside() const { return shown_temp_; }               /* degrees, as drawn (tenths) */
    int target() const { return target_; }                     /* the thermostat's degrees */
    bool heating() const { return shown_temp_ < target_ - 0.05f; }
    bool cooling() const { return shown_temp_ > target_ + 0.05f; }
    bool playing() const { return playing_; }
    int music_room() const { return music_room_; }
    const char *song() const;                                   /* "Blue in Green" */
    const char *media() const { return media_; }                /* "jazz" */
    int volume() const { return volume_; }                      /* 0-10 */

private:
    void rooms_of(const voice::Action &a, bool all_by_default, bool *out) const;
    void doors_of(const voice::Action &a, bool *out) const;

    int light_[ROOMS], last_level_[ROOMS];
    float blinds_[ROOMS], blinds_to_[ROOMS];
    bool locked_[DOORS];
    float temp_, shown_temp_;
    int target_;
    bool playing_;
    int music_room_, song_, volume_;
    char media_[24];
};

} // namespace smarthome
