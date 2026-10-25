// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The factory line's world (demo::line()): the stack light and the line's state, three stations (their tools move
 * when a box passes), the belt with its boxes, the speed gauge, the counters; the emergency stop and an alarm as a
 * banner over the stations. While the line runs, only the band of the tools, the belt and the boxes is drawn again.
 */
#include <cmath>
#include <cstdio>
#include "demo.hpp"
#include "ui_internal.hpp"

namespace ui {

namespace {

using factory::Line;

constexpr int BELT_X = 24, BELT_W = 540, BELT_Y = 262, BAND_Y = 186, BAND_H = 126;

int X(int x) { return MAP.x + x; }
int Y(int y) { return MAP.y + y; }
int bx(float x) { return X(BELT_X) + static_cast<int>(x * BELT_W / factory::BELT_LEN); }

Color product_color(int p) { return p == factory::BLUE ? theme->accent : p == factory::GREEN ? theme->ok : theme->danger; }

} // namespace

bool factory_moving(Rect &box)
{
    const Line &l = demo::line();
    box = {MAP.x + 4, MAP.y + BAND_Y, MAP.w - 8, BAND_H};
    bool work = false;
    for (int s = 0; s < 3; s++)
        work = work || l.station_anim(s) > 0;
    return l.running() || work;
}

void draw_factory(Painter &p)
{
    const Line &l = demo::line();
    world_panel(p);
    /* the stack light and the state */
    const bool red = l.estop() || l.alarm()[0];
    const Color lamps[3] = {red ? theme->danger : theme->mark, !red && !l.running() ? theme->warn : theme->mark,
                            l.running() ? theme->ok : theme->mark};
    p.round(X(20), Y(14), 34, 82, 8, theme->soft);
    for (int i = 0; i < 3; i++)
        p.disc(X(37), Y(28 + i * 26), 9, lamps[i]);
    const char *state = l.estop() ? "EMERGENCY STOP" : l.alarm()[0] ? "ALARM" : l.running() ? "RUNNING" : "STOPPED";
    p.fit({X(70), Y(16), 300, 34}, state,
          style(26, l.estop() || l.alarm()[0] ? theme->danger : l.running() ? theme->ok : theme->warn, true), "fac.state");
    char sp[48];
    snprintf(sp, sizeof sp, "%s boxes at %d %%", factory::product_name(l.product()), l.speed());
    p.fit({X(70), Y(52), 300, 22}, sp, style(15, theme->muted), "fac.sub");
    /* the stations */
    for (int s = 0; s < 3; s++) {
        const int cx = bx(factory::STATION_AT[s]);
        const bool jam = s == 1 && l.alarm()[0];
        p.roundFrame(cx - 52, Y(104), 104, 82, 10, theme->line, theme->soft, 1);
        char name[24];
        snprintf(name, sizeof name, "%d %s", s + 1, factory::machine_name(s + 1));
        p.fit({cx - 46, Y(112), 92, 20}, name, style(13, theme->ink, true), "fac.station");
        p.disc(cx - 39, Y(146), 6, jam ? theme->danger : l.station_on(s) ? theme->ok : theme->mark);
        p.fit({cx - 28, Y(137), 74, 18}, jam ? "jammed" : l.station_on(s) ? "on" : "off",
              style(13, jam ? theme->danger : theme->muted), "fac.on");
        /* its tool, down to the belt as it works a box */
        const float w = l.station_anim(s);
        const int down = static_cast<int>(28 * std::sin(w * 3.14159f));
        if (s == 0) {
            p.rect(cx - 3, Y(186), 6, 10 + down, theme->mark);
            p.round(cx - 22, Y(196) + down, 44, 22, 4, theme->ink);
        } else if (s == 1) {
            const float a = -1.2f + 0.9f * w;
            p.disc(cx, Y(194), 7, theme->ink);
            p.line(cx, Y(194), cx + static_cast<int>(40 * std::cos(a)), Y(194) + static_cast<int>(44 * std::sin(-a) + 20),
                   theme->ink, 5);
        } else {
            p.line(cx - 24, Y(204) + down, cx - 4, Y(222) + down, theme->ink, 4);
            p.line(cx + 24, Y(204) + down, cx + 4, Y(222) + down, theme->ink, 4);
        }
    }
    /* the belt: its stripes move with it, its boxes */
    p.round(X(BELT_X - 8), Y(BELT_Y), BELT_W + 16, 40, 14, theme->mark);
    p.round(X(BELT_X - 4), Y(BELT_Y + 4), BELT_W + 8, 32, 12, pu::mix(theme->bg, theme->mark, 0.5f));
    const int ph = static_cast<int>(std::fmod(l.belt_phase() * BELT_W / factory::BELT_LEN, 24.0f));
    for (int x = ph; x < BELT_W; x += 24)
        p.rect(X(BELT_X) + x, Y(BELT_Y + 6), 3, 28, theme->mark);
    p.disc(X(BELT_X), Y(BELT_Y + 20), 13, theme->line);
    p.disc(X(BELT_X + BELT_W), Y(BELT_Y + 20), 13, theme->line);
    const factory::Box *boxes;
    const int nb = l.boxes(&boxes);
    for (int i = 0; i < nb; i++) {
        const int x = bx(boxes[i].x);
        const Color c = product_color(boxes[i].product);
        p.round(x - 15, Y(BELT_Y - 22), 30, 30, 4, c);
        for (int s = 0; s < 3; s++) /* the stations that worked on it */
            if (boxes[i].marks & (1u << s))
                p.disc(x - 8 + s * 8, Y(BELT_Y - 12), 3, theme->ink);
    }
    /* the speed, the counters, the product */
    const int gx = X(96), gy = Y(420);
    arc(p, gx, gy, 54, 54, 3.14159f, 6.28318f, theme->mark, 8);
    arc(p, gx, gy, 54, 54, 3.14159f, 3.14159f * (1 + l.speed() / 100.0f), theme->accent, 8);
    char v[16];
    snprintf(v, sizeof v, "%d %%", l.speed());
    p.fit({gx - 50, gy - 34, 100, 30}, v, style(22, theme->ink, true, Align::CENTER), "fac.speed");
    p.fit({gx - 50, gy + 4, 100, 20}, "speed", style(13, theme->muted, false, Align::CENTER), "fac.speedl");
    char n[16], m[40];
    snprintf(n, sizeof n, "%d", l.count());
    snprintf(m, sizeof m, l.rejects() ? "made, %d missed a station" : "made", l.rejects());
    p.fit({X(200), Y(362), 160, 52}, n, style(44, theme->ink, true), "fac.count");
    p.fit({X(200), Y(416), 200, 20}, m, style(14, theme->muted), "fac.made");
    p.round(X(440), Y(368), 40, 40, 6, product_color(l.product()));
    p.fit({X(420), Y(414), 120, 20}, factory::product_name(l.product()), style(14, theme->muted, false, Align::CENTER),
          "fac.product");
    /* the emergency stop, an alarm */
    if (l.estop() || l.alarm()[0]) {
        const Color c = l.estop() ? theme->danger : theme->warn;
        p.round(X(20), Y(100), 548, 92, 12, c);
        p.fit({X(36), Y(110), 516, 36}, l.estop() ? "EMERGENCY STOP" : "ALARM",
              style(28, theme->on_accent, true, Align::CENTER), "fac.banner");
        char why[96];
        snprintf(why, sizeof why, l.estop() ? "say \"reset the emergency stop\"" : "%s: say \"reset the alarm\"",
                 l.alarm());
        p.fit({X(36), Y(150), 516, 28}, why, style(17, theme->on_accent, false, Align::CENTER), "fac.why");
    }
    draw_world_status(p);
}

} // namespace ui
