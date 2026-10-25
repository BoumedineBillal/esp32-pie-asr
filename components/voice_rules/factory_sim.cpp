// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * factory_sim.hpp. A refused order (start during an emergency stop or an alarm) is shown skipped, with the answer
 * saying why.
 */
#include "factory_sim.hpp"
#include <cstring>

namespace factory {

namespace {

constexpr float GAP = 60.0f;            /* map units between two boxes */
constexpr float JAM_AFTER = 60.0f;      /* seconds of running */
constexpr float WORK_TIME = 0.6f;       /* a station's stroke */

float belt_speed(int pct) { return 30.0f + 1.5f * pct; }   /* map units a second: 105 at 50 % */

} // namespace

void Line::reset()
{
    running_ = estop_ = false;
    for (int s = 0; s < 3; s++) {
        station_[s] = true;
        work_[s] = 0;
    }
    speed_ = 50;
    product_ = RED;
    count_ = rejects_ = 0;
    alarm_[0] = 0;
    nbox_ = 0;
    phase_ = spawn_ = run_time_ = 0;
    jammed_once_ = false;
    clear();
}

void Line::start_line(int i)
{
    if (estop_ || alarm_[0]) {
        say("Refused: %s. Reset it first.", estop_ ? "the emergency stop is on" : alarm_);
        act_[i] = SKIPPED;
        return;
    }
    running_ = true;
}

void Line::order(const voice::Result &r)
{
    begin(r);
    for (int i = 0; i < r.n; i++) {
        const voice::Action &a = r.actions[i];
        if (a.skipped)
            continue;
        switch (a.rule) {
        case ESTOP:
            estop_ = true;
            running_ = false;
            for (int s = 0; s < 3; s++)
                station_[s] = false;
            break;
        case START: {
            const int m = machine(a);
            if (m == LINE || m < 0) {
                start_line(i);
                if (running_)
                    for (int s = 0; s < 3; s++)
                        station_[s] = true;
            } else if (estop_) {
                say("Refused: the emergency stop is on. Reset it first.");
                act_[i] = SKIPPED;
            } else {
                station_[m - 1] = true;
            }
            break;
        }
        case STOP: {
            const int m = machine(a);
            if (m == LINE || m < 0)
                running_ = false;
            else
                station_[m - 1] = false;
            break;
        }
        case RESET:
            if (!estop_ && !alarm_[0]) {
                say("There is nothing to reset.");
                act_[i] = ANSWERED;
            }
            estop_ = false;
            alarm_[0] = 0;
            run_time_ = 0;
            for (int s = 0; s < 3; s++)
                station_[s] = true;
            break;
        case SET_SPEED:
            speed_ = a.value < 10 ? 10 : a.value > 100 ? 100 : a.value;
            if (speed_ != a.value)
                say("The speed goes from 10 to 100 %%: set to %d %%.", speed_);
            break;
        case FASTER:
            speed_ = speed_ + 20 > 100 ? 100 : speed_ + 20;
            break;
        case SLOWER:
            speed_ = speed_ - 20 < 10 ? 10 : speed_ - 20;
            break;
        case PRODUCT:
            if (factory::product(a) >= 0)
                product_ = factory::product(a);
            break;
        case STATUS:
            say("%s at %d %%, %d %s boxes made%s%s%s.", running_ ? "Running" : "Stopped", speed_, count_,
                product_name(product_), rejects_ ? ", " : "", rejects_ ? "some missed a station" : "",
                estop_ ? ", emergency stop on" : alarm_[0] ? ", alarm" : ", no alarms");
            act_[i] = ANSWERED;
            break;
        default: break;
        }
    }
}

void Line::update(float dt)
{
    if (dt <= 0)
        return;
    for (int s = 0; s < 3; s++)
        if (work_[s] > 0)
            work_[s] = work_[s] - dt / WORK_TIME < 0 ? 0 : work_[s] - dt / WORK_TIME;
    if (!running_)
        return;
    const float v = belt_speed(speed_) * dt;
    phase_ += v;
    if (phase_ > 1000.0f)
        phase_ -= 1000.0f;
    /* boxes on, along, off */
    spawn_ -= v;
    if (spawn_ <= 0 && nbox_ < MAX_BOXES && (nbox_ == 0 || box_[nbox_ - 1].x >= GAP)) {
        box_[nbox_++] = {0.0f, product_, 0};
        spawn_ = GAP;
    }
    int w = 0;
    for (int b = 0; b < nbox_; b++) {
        Box bx = box_[b];
        const float before = bx.x;
        bx.x += v;
        for (int s = 0; s < 3; s++)
            if (before < STATION_AT[s] && bx.x >= STATION_AT[s] && station_[s]) {
                bx.marks |= 1u << s;
                work_[s] = 1.0f;
            }
        if (bx.x >= BELT_LEN) {
            if (bx.marks == 7)
                count_++;
            else
                rejects_++;
            version_++;
            continue;
        }
        box_[w++] = bx;
    }
    nbox_ = w;
    /* the arm's jam, once per reset */
    run_time_ += dt;
    if (run_time_ >= JAM_AFTER && !jammed_once_ && station_[1]) {
        jammed_once_ = true;
        running_ = false;
        snprintf(alarm_, sizeof alarm_, "jam at station 2 (the robot arm)");
        version_++;
    }
}

const char *Line::status(char *out, int cap) const
{
    if (estop_)
        snprintf(out, cap, "EMERGENCY STOP: say \"reset the emergency stop\"");
    else if (alarm_[0])
        snprintf(out, cap, "Alarm: %s. Say \"reset the alarm\"", alarm_);
    else
        snprintf(out, cap, "%s at %d %%, %s boxes, %d made", running_ ? "Running" : "Stopped", speed_,
                 product_name(product_), count_);
    return out;
}

} // namespace factory
