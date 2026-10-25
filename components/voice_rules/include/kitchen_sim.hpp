// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The kitchen, simulated: four burners (levels 0-9), an oven that heats to its setting (a demo's oven: 12 degrees a
 * second), up to three timers counting down (one that ends rings for 10 s), a kettle that boils in 15 s and switches
 * itself off, the extractor hood, the lights. Deterministic: update(dt) only.
 */
#pragma once

#include "kitchen_rules.hpp"
#include "world_base.hpp"

namespace kitchen {

constexpr int MAX_TIMERS = 3;

class Kitchen : public voice::BasicWorld {
public:
    Kitchen() { reset(); }
    void reset() override;
    void order(const voice::Result &r) override;
    void update(float dt) override;
    const char *status(char *out, int cap) const override;

    int burner(int b) const { return burner_[b]; }          /* 0 off, 1-9 */
    bool oven_on() const { return oven_on_; }
    int oven_target() const { return oven_target_; }
    int oven_temp() const { return shown_oven_; }            /* as drawn (5-degree steps) */
    bool oven_ready() const { return oven_on_ && oven_ >= oven_target_ - 2; }
    int timers(int *left, int *total, bool *ringing) const;  /* seconds left / set, each (left 0: ringing) */
    bool kettle_on() const { return kettle_on_; }
    int kettle_temp() const { return shown_kettle_; }        /* as drawn (5-degree steps) */
    bool kettle_boiled() const { return boiled_ > 0; }       /* for a few seconds after */
    bool hood() const { return hood_; }
    bool lights() const { return lights_; }

private:
    enum Wait { NOTHING, OVEN, KETTLE_BOIL, TIMER };
    Wait wait_[voice::MAX_ACTIONS];
    int burner_[BURNERS];
    bool oven_on_;
    int oven_target_, shown_oven_;
    float oven_;
    float left_[MAX_TIMERS], ring_[MAX_TIMERS];
    int total_[MAX_TIMERS], ntimers_, shown_secs_;
    bool kettle_on_;
    float kettle_, boiled_;
    int shown_kettle_;
    bool hood_, lights_;
};

} // namespace kitchen
