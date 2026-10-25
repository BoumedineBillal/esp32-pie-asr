// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The robot vacuum's world: the flat and the robot (demo::vacuum()). Map units are pixels from the panel's top-left
 * (MAP). The robot moves every frame: its box is drawn again as it goes.
 */
#include <cmath>
#include "demo.hpp"
#include "ui_internal.hpp"

namespace ui {

using vacuum::Point;

static int sx(float x) { return MAP.x + static_cast<int>(std::lround(x)); }
static int sy(float y) { return MAP.y + static_cast<int>(std::lround(y)); }

/* a dashed line between two screen points: dash on, gap off */
static void dashed(Painter &p, float x0, float y0, float x1, float y1, Color c, int t)
{
    const float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    constexpr float DASH = 8, GAP = 6;
    for (float a = 0; a < len; a += DASH + GAP) {
        const float b = a + DASH < len ? a + DASH : len;
        p.line(static_cast<int>(x0 + (x1 - x0) * a / len), static_cast<int>(y0 + (y1 - y0) * a / len),
               static_cast<int>(x0 + (x1 - x0) * b / len), static_cast<int>(y0 + (y1 - y0) * b / len), c, t);
    }
}

static void crumbs(Painter &p, int x, int y, Color c, int r)
{
    static const int8_t D[5][2] = {{0, 0}, {8, 10}, {3, 18}, {18, -4}, {20, 6}};
    for (const auto &d : D)
        p.disc(x + d[0], y + d[1], r, c);
}

bool vacuum_moving(Rect &box)
{
    const Point at = demo::vacuum().pos();
    box = {sx(at.x) - 28, sy(at.y) - 28, 56, 56};
    return false; /* (drawn again when the box moves) */
}

void draw_vacuum(Painter &p)
{
    const vacuum::Sim &s = demo::vacuum();
    world_panel(p);
    const Color fl = floor_color(), wall = theme->mark;
    /* the rooms, the doorways, the furniture */
    for (int r = 0; r < vacuum::ROOMS; r++) {
        const vacuum::Box &b = vacuum::ROOM_BOX[r];
        const Color c = s.room_clean(r) ? pu::mix(fl, theme->ok, 0.14f) : fl;
        p.roundFrame(sx(b.x), sy(b.y), b.w, b.h, 14, wall, c, 3);
    }
    for (const vacuum::Door &d : vacuum::DOOR)
        p.rect(sx(d.gap.x), sy(d.gap.y), d.gap.w, d.gap.h, fl);
    for (const vacuum::Box &f : vacuum::FURNITURE_BOX)
        p.roundFrame(sx(f.x), sy(f.y), f.w, f.h, 6, theme->line, theme->soft, 1);
    /* the lanes cleaned, and the one being cleaned up to the robot */
    const Color lane_c = pu::mix(fl, theme->ok, 0.55f);
    for (int r = 0; r < vacuum::ROOMS; r++)
        for (int i = 0; i < s.lanes_done(r); i++) {
            Point a, b;
            vacuum::lane(r, i, a, b);
            p.line(sx(a.x), sy(a.y), sx(b.x), sy(b.y), lane_c, 3);
        }
    int lr, li;
    Point la, lb;
    if (s.cleaning_lane(lr, li, la, lb))
        p.line(sx(la.x), sy(la.y), sx(lb.x), sy(lb.y), lane_c, 3);
    /* the rooms' names and marks (over the lanes) */
    for (int r = 0; r < vacuum::ROOMS; r++) {
        const vacuum::Box &b = vacuum::ROOM_BOX[r];
        const TextStyle name = style(15, theme->muted, true);
        const Rect nb = p4lcd::Canvas::textBox(0, 0, vacuum::room_name(r), name);
        p.rect(sx(b.x) + 12, sy(b.y) + 10, nb.w + 8, nb.h + 4, /* the lanes pass under the name */
               s.room_clean(r) ? pu::mix(fl, theme->ok, 0.14f) : fl);
        p.fit({sx(b.x) + 16, sy(b.y) + 12, 150, 22}, vacuum::room_name(r), name, "vac.room");
        if (s.room_clean(r))
            pu::pill(p, sx(b.x + b.w) - 96, sy(b.y) + 10, 24, "cleaned", theme->ok, theme->on_accent, 13);
        else if (s.room_skipped(r))
            pu::pill(p, sx(b.x + b.w) - 96, sy(b.y) + 10, 24, "skipped", theme->soft, theme->muted, 13);
    }
    /* the messes still there */
    const TextStyle lab = style(13, theme->muted, false, Align::CENTER, VAlign::TOP);
    for (int k = 0; k < vacuum::SPOTS; k++) {
        if (s.spot_clean(k))
            continue;
        const int x = sx(vacuum::SPOT_AT[k].x), y = sy(vacuum::SPOT_AT[k].y);
        switch (k) {
        case vacuum::SPILL:
            p.disc(x, y, 24, theme->warn);
            p.disc(x, y, 22, pu::mix(fl, theme->warn, 0.25f));
            p.text(x, y + 30, "spill", lab);
            break;
        case vacuum::DUST:
            for (int i = 0; i < 8; i++)
                p.disc(x - 15 + (i * 7) % 30, y - 10 + (i * 11) % 22, 2, theme->faint);
            p.text(x, y + 18, "dust", lab);
            break;
        default:
            crumbs(p, x - 10, y - 8, theme->warn, 3);
            p.text(x, y + 20, vacuum::spot_name(k), lab);
            break;
        }
    }
    /* the dock */
    p.round(sx(vacuum::DOCK.x) - 22, sy(vacuum::DOCK.y) + 20, 44, 12, 4, theme->mark);
    p.text(sx(vacuum::DOCK.x), sy(vacuum::DOCK.y) + 36, "dock", lab);
    /* the way still to drive */
    Point way[8];
    const int nw = s.route(way, 8);
    for (int i = 0; i + 1 < nw; i++)
        dashed(p, sx(way[i].x), sy(way[i].y), sx(way[i + 1].x), sy(way[i + 1].y), theme->accent, 3);
    /* the robot */
    const Point at = s.pos();
    const int cx = sx(at.x), cy = sy(at.y);
    const Color ring = s.mode() == vacuum::Sim::PAUSED ? theme->warn : theme->accent;
    p.disc(cx, cy, vacuum::ROBOT_R + 3, ring);
    p.disc(cx, cy, vacuum::ROBOT_R, pu::mix(theme->panel, ring, 0.3f));
    p.disc(cx, cy, 5, ring);
    p.line(cx, cy, cx + static_cast<int>(15 * std::cos(s.heading())), cy + static_cast<int>(15 * std::sin(s.heading())),
           ring, 3);
    draw_world_status(p);
}

} // namespace ui
