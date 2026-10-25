// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The robot vacuum's flat and its robot, simulated: the screen draws what this holds, the rules' actions drive it.
 *
 * The flat in map units (the screen's map panel is 588 x 504 of them): the kitchen at the top left with the dock,
 * the bedroom below it, the living room on the right; a doorway between each pair. The robot drives in straight
 * lines through the doorways, cleans a room in back-and-forth lanes, circles a mess, and charges at the dock.
 *
 * A new order replaces what was left to do (an utterance that continues the previous one adds to it); "stop" pauses,
 * "resume" goes on; the suction and the status answer at once. When its last task was a cleaning, the robot goes
 * back to the dock by itself (an order on the way replaces that). Deterministic: update(dt) only.
 */
#pragma once

#include <cstdint>
#include "vacuum_rules.hpp"

namespace vacuum {

struct Box {
    int16_t x, y, w, h;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
struct Point {
    float x, y;
};

/* the flat */
constexpr int MAP_W = 588, MAP_H = 504;
constexpr Box ROOM_BOX[ROOMS] = {{20, 20, 270, 250}, {302, 20, 266, 454}, {20, 282, 270, 192}};
struct Door {
    int a, b;                  /* the rooms it joins */
    Point at;                  /* its middle */
    Box gap;                   /* the opening in the walls */
};
constexpr int DOORS = 3;
constexpr Door DOOR[DOORS] = {{KITCHEN, LIVING_ROOM, {296, 200}, {284, 178, 24, 44}},
                              {BEDROOM, LIVING_ROOM, {296, 420}, {284, 398, 24, 44}},
                              {KITCHEN, BEDROOM, {150, 276}, {128, 264, 44, 24}}};
constexpr Point DOCK = {66, 226};
constexpr Point SPOT_AT[SPOTS] = {{200, 150}, {118, 176}, {430, 430}, {165, 420}};
constexpr int SPOT_ROOM[SPOTS] = {KITCHEN, KITCHEN, LIVING_ROOM, BEDROOM};
constexpr int FURNITURE = 6;
constexpr Box FURNITURE_BOX[FURNITURE] = {{110, 54, 150, 30},  /* the kitchen counter */
                                          {320, 60, 160, 70},  /* the sofa */
                                          {360, 158, 90, 46},  /* the table */
                                          {534, 60, 14, 140},  /* the television */
                                          {40, 318, 136, 146}, /* the bed */
                                          {228, 320, 44, 140}};/* the wardrobe */
constexpr int ROBOT_R = 18;

/* the lanes of a room's cleaning */
constexpr int MAX_LANES = 18;
int lanes(int room);                       /* how many */
void lane(int room, int i, Point &a, Point &b);   /* lane i, from a to b (back and forth) */

class Sim : public voice::World {
public:
    enum Mode { DOCKED, IDLE, MOVING, CLEANING_ROOM, CLEANING_SPOT, PAUSED };

    Sim() { reset(); }
    void reset() override;                 /* at the dock, charged, the flat dirty, nothing to do */

    /* an utterance's actions: the order (instant answers now, moves queued); continued: added to what is left */
    void order(const voice::Result &r) override;
    void update(float dt) override;        /* seconds */

    /* for the screen */
    Point pos() const { return pos_; }
    float heading() const { return heading_; }      /* radians, 0 = +x */
    Mode mode() const { return mode_; }
    int room_now() const;                  /* the room the robot is in (-1: a doorway) */
    bool room_clean(int r) const { return room_clean_[r]; }
    bool room_skipped(int r) const { return room_skipped_[r]; }
    int lanes_done(int r) const { return lanes_done_[r]; }
    bool cleaning_lane(int &room, int &i, Point &from, Point &to) const; /* the lane being cleaned, up to the robot */
    bool spot_clean(int s) const { return spot_clean_[s]; }
    int battery() const { return static_cast<int>(battery_ + 0.5f); }
    int suction() const { return suction_; }
    Step step(int action) const override;  /* of the last order's action i */
    int steps() const override { return nact_; }
    const char *answer() const override { return answer_; } /* the last status answer ("" if none) */
    /* the way still to drive: the robot, then up to n points of the current task's route; returns how many */
    int route(Point *out, int n) const;
    /* "Cleaning the kitchen · battery 92 % · suction medium" */
    const char *status(char *out, int cap) const override;
    /* changes with the route, the tasks, the actions' steps, what is clean or skipped, the mode; not with the robot's
     * position, the lanes it is drawing or the battery (the screen redraws the robot's box and compares status()) */
    uint32_t version() const override { return version_; }

private:
    enum Kind { GOTO, CLEAN_ROOM_T, CLEAN_SPOT_T, DOCK_T };
    struct Task {
        Kind kind;
        int arg;                           /* room or spot */
        int action;                        /* the action it came from (-1: none left on the screen; AUTO) */
    };
    static constexpr int AUTO = -2;        /* the robot's own: back to the dock after a cleaning */
    static constexpr int MAX_TASKS = 12, MAX_WAY = 48;

    void start_task();                     /* the current task's route */
    void finish_task();
    void add(Kind k, int arg, int action);
    void route_to(Point to, int room_to);  /* doorways from where the robot is */

    Point pos_;
    float heading_;
    Mode mode_, before_pause_;
    Task tasks_[MAX_TASKS];
    int ntasks_, cur_;                     /* the tasks, the one running (ntasks_: none) */
    Point way_[MAX_WAY];
    int nway_, wi_;                        /* the route, the point driven to */
    int lane_room_, lane_i_;               /* while cleaning a room: its lane being cleaned (way_ holds the lanes) */
    float spin_;                           /* while circling a mess: the angle done */
    bool room_clean_[ROOMS], room_skipped_[ROOMS], spot_clean_[SPOTS];
    int lanes_done_[ROOMS];
    float battery_;
    int suction_;
    Step act_[voice::MAX_ACTIONS];
    int nact_;
    char answer_[96];
    uint32_t version_ = 0;
};

} // namespace vacuum
