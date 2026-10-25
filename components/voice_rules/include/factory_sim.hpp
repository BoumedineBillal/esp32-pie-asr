// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The factory line, simulated: boxes come onto a conveyor (map units 0 .. BELT_LEN) and pass three stations (the
 * press, the robot arm, the packer), each of which marks them when it is on; a box with all three marks is counted at
 * the end. The belt's speed follows the speed setting. After 60 s of running since the last reset the arm jams: the
 * line stops with an alarm until it is reset. The emergency stop stops everything and refuses a start until it is
 * reset. Deterministic: update(dt) only.
 */
#pragma once

#include "factory_rules.hpp"
#include "world_base.hpp"

namespace factory {

constexpr float BELT_LEN = 520.0f;
constexpr float STATION_AT[3] = {130.0f, 270.0f, 410.0f};   /* the stations along the belt */
constexpr int MAX_BOXES = 10;

struct Box {
    float x;           /* along the belt */
    int product;       /* RED, BLUE, GREEN */
    uint8_t marks;     /* bit s: station s worked on it */
};

class Line : public voice::BasicWorld {
public:
    Line() { reset(); }
    void reset() override;
    void order(const voice::Result &r) override;
    void update(float dt) override;
    const char *status(char *out, int cap) const override;

    bool running() const { return running_; }
    bool station_on(int s) const { return station_[s]; }   /* s: 0 press, 1 arm, 2 packer */
    int speed() const { return speed_; }                     /* % */
    int product() const { return product_; }
    bool estop() const { return estop_; }
    const char *alarm() const { return alarm_; }             /* "" when none */
    int count() const { return count_; }                     /* boxes made */
    int rejects() const { return rejects_; }                 /* boxes that missed a station */
    int boxes(const Box **out) const { *out = box_; return nbox_; }
    float belt_phase() const { return phase_; }              /* the belt's stripes, map units */
    float station_anim(int s) const { return work_[s]; }     /* 0..1 while a station works a box */

private:
    void start_line(int i);

    bool running_, station_[3], estop_;
    int speed_, product_, count_, rejects_;
    char alarm_[48];
    Box box_[MAX_BOXES];
    int nbox_;
    float phase_, spawn_, run_time_, work_[3];
    bool jammed_once_;
};

} // namespace factory
