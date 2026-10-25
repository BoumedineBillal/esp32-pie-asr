// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The car, simulated (parked, then driving a route): two climate zones the cabin follows (0.2 degree a second), a
 * fan, four windows (2 s to open), two heated seats, the windscreen's defrost (60 s), a route counting down at 60
 * km/h, the media, the headlights, the battery and its range. Deterministic: update(dt) only.
 */
#pragma once

#include "car_rules.hpp"
#include "world_base.hpp"

namespace car {

class Car : public voice::BasicWorld {
public:
    Car() { reset(); }
    void reset() override;
    void order(const voice::Result &r) override;
    void update(float dt) override;
    const char *status(char *out, int cap) const override;

    int target(int zone) const { return target_[zone]; }      /* zone: DRIVER, PASSENGER */
    float cabin(int zone) const { return shown_[zone]; }      /* as drawn (tenths) */
    int fan() const { return fan_; }                          /* 0-5 */
    float window(int w) const { return win_[w]; }             /* 0 closed .. 1 open */
    int seat(int zone) const { return seat_[zone]; }          /* 0 off, 3 on */
    bool defrosting() const { return defrost_ > 0; }
    int destination() const { return dest_; }                 /* -1: no route */
    float distance() const { return shown_km_; }              /* km left (tenths) */
    int eta_minutes() const;
    bool playing() const { return playing_; }
    const char *media() const { return media_; }
    const char *track() const;
    int volume() const { return volume_; }                    /* 0-10 */
    bool headlights() const { return lights_; }
    int battery() const { return static_cast<int>(battery_ + 0.5f); }
    int range_km() const { return static_cast<int>(battery_ * 4.5f + 0.5f); }

private:
    enum Wait { NOTHING, WINDOWS_MOVE, DEFROSTING, ROUTE };
    Wait wait_[voice::MAX_ACTIONS];     /* what a RUNNING step waits for */
    float temp_[2], shown_[2];
    int target_[2], fan_;
    float win_[WINDOWS], win_to_[WINDOWS];
    int seat_[2];
    float defrost_;
    int dest_;
    float km_, shown_km_;
    bool playing_;
    char media_[24];
    int track_, volume_;
    bool lights_;
    float battery_;
};

} // namespace car
