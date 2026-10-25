// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * vacuum_sim.hpp: the tasks an order makes (a room to go to or clean, a mess, the dock), the route through the
 * doorways, the lanes, the battery.
 */
#include "vacuum_sim.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vacuum {

namespace {

constexpr float TRAVEL = 260.0f;       /* map units per second, driving */
constexpr float CLEANING = 220.0f;     /* along a room's lanes */
constexpr float SPIN = 2.4f;           /* radians per second around a mess */
constexpr float SPOT_R = 26.0f;        /* the circle around a mess */
constexpr float TWO_PI = 6.2831853f;
constexpr int LANE_MARGIN = 26, LANE_STEP = 28;
constexpr float DRAIN = 1.0f / 8.0f;   /* battery % per second while working */
constexpr float CHARGE = 2.0f;         /* % per second at the dock */

Point center(int room)
{
    const Box &b = ROOM_BOX[room];
    return {b.x + b.w / 2.0f, b.y + b.h / 2.0f};
}

const Door *door_between(int a, int b)
{
    for (const Door &d : DOOR)
        if ((d.a == a && d.b == b) || (d.a == b && d.b == a))
            return &d;
    return nullptr;
}

float dist(Point a, Point b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

bool mover(int rule)
{
    return rule == CLEAN_SPOT || rule == CLEAN_ROOM || rule == CLEAN_ALL || rule == GO_TO_ROOM ||
           rule == RETURN_TO_DOCK;
}

} // namespace

int lanes(int room)
{
    const Box &b = ROOM_BOX[room];
    const int h = b.h - 2 * LANE_MARGIN;
    const int n = h / LANE_STEP + 1;
    return n < MAX_LANES ? n : MAX_LANES;
}

void lane(int room, int i, Point &a, Point &b)
{
    const Box &r = ROOM_BOX[room];
    const int n = lanes(room);
    const float top = r.y + LANE_MARGIN, h = r.h - 2.0f * LANE_MARGIN;
    const float y = n > 1 ? top + h * i / (n - 1) : top + h / 2;
    const float x0 = r.x + LANE_MARGIN, x1 = r.x + r.w - LANE_MARGIN;
    a = {i % 2 ? x1 : x0, y};
    b = {i % 2 ? x0 : x1, y};
}

void Sim::reset()
{
    pos_ = DOCK;
    heading_ = 0;
    mode_ = before_pause_ = DOCKED;
    ntasks_ = cur_ = 0;
    nway_ = wi_ = 0;
    lane_room_ = lane_i_ = -1;
    spin_ = 0;
    for (int r = 0; r < ROOMS; r++) {
        room_clean_[r] = room_skipped_[r] = false;
        lanes_done_[r] = 0;
    }
    for (int s = 0; s < SPOTS; s++)
        spot_clean_[s] = false;
    battery_ = 100.0f;
    suction_ = MEDIUM;
    nact_ = 0;
    answer_[0] = 0;
    version_++;
}

int Sim::room_now() const
{
    for (int r = 0; r < ROOMS; r++)
        if (ROOM_BOX[r].contains(pos_.x, pos_.y))
            return r;
    return -1;
}

void Sim::add(Kind k, int arg, int action)
{
    if (ntasks_ < MAX_TASKS)
        tasks_[ntasks_++] = {k, arg, action};
}

void Sim::route_to(Point to, int room_to)
{
    nway_ = wi_ = 0;
    int from = room_now();
    if (from < 0) { /* in a doorway: from the room of the doorway that leads on */
        for (const Door &d : DOOR)
            if (d.gap.contains(pos_.x, pos_.y) || dist(pos_, d.at) < 30) {
                from = (d.a == room_to || d.b == room_to) ? room_to : d.a;
                break;
            }
    }
    if (from >= 0 && from != room_to)
        if (const Door *d = door_between(from, room_to))
            way_[nway_++] = d->at;
    way_[nway_++] = to;
}

void Sim::start_task()
{
    const Task &t = tasks_[cur_];
    if (t.action >= 0 && t.action < nact_ && act_[t.action] == QUEUED)
        act_[t.action] = RUNNING;
    Point a, b;
    switch (t.kind) {
    case GOTO: route_to(center(t.arg), t.arg); break;
    case CLEAN_ROOM_T: lane(t.arg, 0, a, b); route_to(a, t.arg); break;
    case CLEAN_SPOT_T: route_to({SPOT_AT[t.arg].x + SPOT_R, SPOT_AT[t.arg].y}, SPOT_ROOM[t.arg]); break;
    case DOCK_T: route_to(DOCK, KITCHEN); break;
    }
    mode_ = MOVING;
    version_++;
}

void Sim::finish_task()
{
    const Task t = tasks_[cur_];
    cur_++;
    if (t.action >= 0 && t.action < nact_) { /* done when no later task is the same action's */
        bool more = false;
        for (int i = cur_; i < ntasks_; i++)
            more = more || tasks_[i].action == t.action;
        if (!more)
            act_[t.action] = DONE;
    }
    lane_room_ = lane_i_ = -1;
    if (cur_ >= ntasks_ && (t.kind == CLEAN_ROOM_T || t.kind == CLEAN_SPOT_T)) { /* done cleaning: home */
        ntasks_ = cur_ = 0;
        add(DOCK_T, 0, AUTO);
    }
    if (cur_ < ntasks_)
        start_task();
    else
        mode_ = t.kind == DOCK_T ? DOCKED : IDLE;
    version_++;
}

void Sim::order(const voice::Result &r)
{
    bool moves = false;
    for (int i = 0; i < r.n; i++)
        moves = moves || (mover(r.actions[i].rule) && !r.actions[i].skipped);
    /* the screen lists this order's actions */
    nact_ = r.n;
    for (int i = 0; i < r.n; i++)
        act_[i] = r.actions[i].skipped ? SKIPPED : mover(r.actions[i].rule) ? QUEUED : ANSWERED;
    if (moves) {
        if (r.continued) { /* added to what is left: the earlier tasks no longer have actions on the screen */
            if (cur_ < ntasks_ && tasks_[cur_].action == AUTO) /* (not after the way home) */
                ntasks_ = cur_;
            for (int i = cur_; i < ntasks_; i++)
                tasks_[i].action = -1;
            if (cur_ > 0) {
                const int left = ntasks_ - cur_;
                memmove(tasks_, tasks_ + cur_, sizeof(Task) * left);
                ntasks_ = left;
                cur_ = 0;
            }
        } else {
            ntasks_ = cur_ = 0;
            for (int k = 0; k < ROOMS; k++)
                room_skipped_[k] = false;
        }
    }
    const bool was_idle = cur_ >= ntasks_;
    /* the rooms cancelled in this order ("not the bedroom") */
    bool skip[ROOMS] = {};
    for (int i = 0; i < r.n; i++)
        if (r.actions[i].skipped && room(r.actions[i]) >= 0) {
            skip[room(r.actions[i])] = true;
            room_skipped_[room(r.actions[i])] = true;
        }
    for (int i = 0; i < r.n; i++) {
        const voice::Action &a = r.actions[i];
        if (a.skipped)
            continue;
        switch (a.rule) {
        case CLEAN_SPOT: add(CLEAN_SPOT_T, spot(a), i); break;
        case CLEAN_ROOM:
            if (skip[room(a)])
                act_[i] = SKIPPED;
            else
                add(CLEAN_ROOM_T, room(a), i);
            break;
        case CLEAN_ALL:
            for (int k = 0; k < ROOMS; k++)
                if (!skip[k])
                    add(CLEAN_ROOM_T, k, i);
            break;
        case GO_TO_ROOM: add(GOTO, room(a), i); break;
        case RETURN_TO_DOCK: add(DOCK_T, 0, i); break;
        case STOP:
            if (mode_ != PAUSED && mode_ != DOCKED && mode_ != IDLE) {
                before_pause_ = mode_;
                mode_ = PAUSED;
            }
            break;
        case RESUME:
            if (mode_ == PAUSED)
                mode_ = before_pause_;
            break;
        case SUCTION:
            if (vacuum::suction(a) >= 0) {
                suction_ = vacuum::suction(a);
                snprintf(answer_, sizeof answer_, "Suction set to %s.", suction_name(suction_));
            }
            break;
        case STATUS: {
            char s[80];
            snprintf(answer_, sizeof answer_, "%s.", status(s, sizeof s));
            break;
        }
        default: break;
        }
    }
    if (moves && (was_idle || !r.continued || mode_ == PAUSED) && cur_ < ntasks_)
        start_task(); /* a new order starts at once, a paused robot too */
    version_++;
}

void Sim::update(float dt)
{
    if (dt <= 0)
        return;
    if (mode_ == DOCKED) { /* (the battery is in status(), which the screen compares: no version change) */
        battery_ = battery_ + CHARGE * dt > 100 ? 100 : battery_ + CHARGE * dt;
    } else if (mode_ == MOVING || mode_ == CLEANING_ROOM || mode_ == CLEANING_SPOT) {
        battery_ = battery_ - DRAIN * dt < 1 ? 1 : battery_ - DRAIN * dt;
    }
    if (mode_ == CLEANING_SPOT) {
        const Task &t = tasks_[cur_];
        spin_ += SPIN * dt;
        const Point c = SPOT_AT[t.arg];
        const float a = spin_ > TWO_PI ? TWO_PI : spin_;
        pos_ = {c.x + SPOT_R * std::cos(a), c.y + SPOT_R * std::sin(a)};
        heading_ = a + TWO_PI / 4;
        if (spin_ >= TWO_PI) {
            spot_clean_[t.arg] = true;
            finish_task();
        }
        return;
    }
    if (mode_ != MOVING && mode_ != CLEANING_ROOM)
        return;
    float left = (mode_ == CLEANING_ROOM ? CLEANING : TRAVEL) * dt;
    while (left > 0 && wi_ < nway_) {
        const Point to = way_[wi_];
        const float d = dist(pos_, to);
        if (d > 0.01f)
            heading_ = std::atan2(to.y - pos_.y, to.x - pos_.x);
        if (d <= left) {
            pos_ = to;
            left -= d;
            wi_++;
            if (mode_ == CLEANING_ROOM && wi_ / 2 > lanes_done_[lane_room_])
                lanes_done_[lane_room_] = wi_ / 2; /* (drawn under the robot as it goes: no version change) */
        } else {
            pos_.x += (to.x - pos_.x) * left / d;
            pos_.y += (to.y - pos_.y) * left / d;
            left = 0;
        }
    }
    if (wi_ < nway_)
        return;
    /* arrived where the route ends */
    const Task &t = tasks_[cur_];
    if (t.kind == CLEAN_ROOM_T && mode_ == MOVING) { /* at lane 0: the lanes */
        nway_ = 0;
        lanes_done_[t.arg] = 0;
        for (int i = 0; i < lanes(t.arg) && nway_ + 2 <= MAX_WAY; i++) {
            Point a, b;
            lane(t.arg, i, a, b);
            way_[nway_++] = a;
            way_[nway_++] = b;
        }
        wi_ = 1;
        lane_room_ = t.arg;
        mode_ = CLEANING_ROOM;
        version_++;
        return;
    }
    if (t.kind == CLEAN_ROOM_T) {
        room_clean_[t.arg] = true;
        for (int s = 0; s < SPOTS; s++)
            if (SPOT_ROOM[s] == t.arg)
                spot_clean_[s] = true;
    }
    if (t.kind == CLEAN_SPOT_T && mode_ == MOVING) { /* beside the mess: around it */
        spin_ = 0;
        mode_ = CLEANING_SPOT;
        version_++;
        return;
    }
    finish_task();
}

bool Sim::cleaning_lane(int &room, int &i, Point &from, Point &to) const
{
    if (mode_ != CLEANING_ROOM && !(mode_ == PAUSED && before_pause_ == CLEANING_ROOM))
        return false;
    if (lane_room_ < 0 || wi_ % 2 == 0) /* between two lanes */
        return false;
    room = lane_room_;
    i = wi_ / 2;
    Point a, b;
    lane(room, i, a, b);
    from = a;
    to = pos_;
    return true;
}

Sim::Step Sim::step(int action) const { return action >= 0 && action < nact_ ? act_[action] : QUEUED; }

int Sim::route(Point *out, int n) const
{
    if (n <= 0 || (mode_ != MOVING && !(mode_ == PAUSED && before_pause_ == MOVING)))
        return 0;
    int k = 0;
    out[k++] = pos_;
    for (int i = wi_; i < nway_ && k < n; i++)
        out[k++] = way_[i];
    return k;
}

const char *Sim::status(char *out, int cap) const
{
    const int r = room_now();
    const char *where = r >= 0 ? room_name(r) : "A doorway";
    char what[48];
    switch (mode_) {
    case DOCKED: snprintf(what, sizeof what, "Docked"); break;
    case IDLE: snprintf(what, sizeof what, "%s, waiting", where); break;
    case MOVING:
        snprintf(what, sizeof what, cur_ < ntasks_ && tasks_[cur_].kind == DOCK_T ? "%s, going back to the dock"
                                                                                  : "%s, driving",
                 where);
        break;
    case CLEANING_ROOM: snprintf(what, sizeof what, "%s, cleaning", where); break;
    case CLEANING_SPOT: snprintf(what, sizeof what, "%s, cleaning the %s", where, spot_name(tasks_[cur_].arg)); break;
    case PAUSED: snprintf(what, sizeof what, "%s, stopped", where); break;
    }
    snprintf(out, cap, "%s, battery %d %%, suction %s", what, battery(), suction_name(suction_));
    return out;
}

} // namespace vacuum
