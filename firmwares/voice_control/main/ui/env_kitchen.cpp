// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The kitchen's world (demo::kitchen()): the hood and the lights, the hob (four burners glowing by their level), the
 * oven (its temperature, its door's glow, ready or heating), the timers counting down (one that ends flashes), the
 * kettle.
 */
#include <cmath>
#include <cstdio>
#include "demo.hpp"
#include "esp_timer.h"
#include "ui_internal.hpp"

namespace ui {

namespace {

using kitchen::Kitchen;

int X(int x) { return MAP.x + x; }
int Y(int y) { return MAP.y + y; }

/* the burners, in kitchen::Burner's order: the front ones on the lower row (nearer the cook), the back ones above */
constexpr int BURNER_AT[kitchen::BURNERS][2] = {{86, 262}, {218, 262}, {86, 132}, {218, 132}};

void box(Painter &p, const Rect &r, const char *title)
{
    p.roundFrame(X(r.x), Y(r.y), r.w, r.h, 12, theme->line, theme->soft, 1);
    if (title)
        p.fit({X(r.x) + 12, Y(r.y) + 8, 160, 20}, title, style(14, theme->muted, true), "kit.box");
}

void fan(Painter &p, int cx, int cy, bool on)
{
    const Color c = on ? theme->accent : theme->mark;
    p.disc(cx, cy, 14, c);
    p.disc(cx, cy, 11, theme->soft);
    for (int i = 0; i < 3; i++) {
        const float a = i * 2.094f + (on ? 0.5f : 0);
        p.line(cx, cy, cx + static_cast<int>(10 * std::cos(a)), cy + static_cast<int>(10 * std::sin(a)), c, 3);
    }
}

} // namespace

void draw_kitchen(Painter &p)
{
    const Kitchen &k = demo::kitchen();
    world_panel(p);
    if (k.lights()) /* the lights: a warmer panel */
        p.round(MAP.x + 2, MAP.y + 2, MAP.w - 4, MAP.h - 4, 11, pu::mix(theme->panel, theme->warn, 0.06f));
    /* the hood and the lights */
    const Rect hood = {20, 12, 548, 40};
    p.roundFrame(X(hood.x), Y(hood.y), hood.w, hood.h, 10, theme->line, theme->soft, 1);
    fan(p, X(44), Y(32), k.hood());
    p.fit({X(66), Y(20), 200, 24}, k.hood() ? "extractor hood: on" : "extractor hood: off",
          style(15, k.hood() ? theme->ink : theme->muted, true, Align::LEFT, VAlign::MIDDLE), "kit.hood");
    p.disc(X(380), Y(32), 9, k.lights() ? theme->warn : theme->mark);
    p.fit({X(396), Y(20), 160, 24}, k.lights() ? "lights: on" : "lights: off",
          style(15, k.lights() ? theme->ink : theme->muted, true, Align::LEFT, VAlign::MIDDLE), "kit.lights");
    /* the hob */
    const Rect hob = {20, 64, 264, 264};
    p.round(X(hob.x), Y(hob.y), hob.w, hob.h, 14, pu::mix(theme->bg, theme->mark, 0.4f));
    for (int b = 0; b < kitchen::BURNERS; b++) {
        const int cx = X(BURNER_AT[b][0]), cy = Y(BURNER_AT[b][1] - 64 + hob.y);
        const int lv = k.burner(b);
        const Color glow = lv ? pu::mix(theme->warn, theme->danger, lv / 9.0f) : theme->mark;
        for (int r = 4; r >= 1; r--)
            p.disc(cx, cy, 12 + r * 9, r % 2 ? glow : pu::mix(theme->bg, theme->mark, 0.4f));
        char lvs[4];
        snprintf(lvs, sizeof lvs, "%d", lv);
        p.text(cx, cy, lv ? lvs : "", style(18, theme->on_accent, true, Align::CENTER, VAlign::MIDDLE));
        p.fit({cx - 60, cy + 47, 120, 16}, kitchen::burner_name(b), style(12, theme->faint, false, Align::CENTER),
              "kit.burner");
    }
    /* the oven */
    const Rect ov = {300, 64, 268, 264};
    box(p, ov, "Oven");
    char t[16], s[32];
    snprintf(t, sizeof t, "%d", k.oven_temp());
    snprintf(s, sizeof s, k.oven_on() ? "of %d degrees" : "off", k.oven_target());
    p.fit({X(ov.x) + 14, Y(ov.y) + 30, 120, 40}, t, style(34, k.oven_on() ? theme->ink : theme->faint, true),
          "kit.oven.t");
    p.fit({X(ov.x) + 112, Y(ov.y) + 44, 140, 22}, s, style(15, theme->muted), "kit.oven.set");
    if (k.oven_on())
        pu::pill(p, X(ov.x) + 168, Y(ov.y) + 8, 24, k.oven_ready() ? "ready" : "heating",
                 pu::mix(theme->panel, k.oven_ready() ? theme->ok : theme->warn, 0.3f),
                 k.oven_ready() ? theme->ok : theme->warn, 13);
    const float heat = (k.oven_temp() - 20) / 230.0f;
    p.roundFrame(X(ov.x) + 24, Y(ov.y) + 86, 220, 132, 10, theme->mark,
                 pu::mix(theme->bg, theme->danger, heat < 0 ? 0 : heat > 1 ? 0.6f : heat * 0.6f), 3);
    p.round(X(ov.x) + 94, Y(ov.y) + 228, 80, 10, 5, theme->mark);
    /* the timers */
    const Rect tm = {20, 340, 264, 112};
    box(p, tm, "Timers");
    int left[kitchen::MAX_TIMERS], total[kitchen::MAX_TIMERS];
    bool ring[kitchen::MAX_TIMERS];
    const int nt = k.timers(left, total, ring);
    if (!nt)
        p.fit({X(tm.x) + 14, Y(tm.y) + 40, 230, 24}, "none: \"set a timer for ...\"", style(15, theme->faint),
              "kit.notimer");
    const bool flash = (esp_timer_get_time() / 500000) % 2;
    for (int i = 0; i < nt; i++) {
        const int y = Y(tm.y) + 32 + i * 26;
        char mm[16];
        snprintf(mm, sizeof mm, "%d:%02d", left[i] / 60, left[i] % 60);
        const Color c = ring[i] ? (flash ? theme->warn : theme->danger) : theme->ink;
        p.fit({X(tm.x) + 14, y, 80, 22}, ring[i] ? "ring!" : mm, style(18, c, true), "kit.timer");
        p.round(X(tm.x) + 100, y + 8, 150, 8, 4, theme->mark);
        const int done = total[i] ? 150 - 150 * left[i] / total[i] : 150;
        if (done > 0)
            p.round(X(tm.x) + 100, y + 8, done < 8 ? 8 : done, 8, 4, ring[i] ? theme->warn : theme->accent);
    }
    /* the kettle */
    const Rect kt = {300, 340, 268, 112};
    box(p, kt, "Kettle");
    const int kx = X(kt.x) + 30, ky = Y(kt.y) + 40;
    p.round(kx, ky, 46, 52, 10, k.kettle_on() ? theme->accent : theme->mark);
    p.line(kx + 46, ky + 14, kx + 60, ky + 4, k.kettle_on() ? theme->accent : theme->mark, 4);
    arc(p, kx, ky + 26, 12, 16, 1.57f, 4.71f, k.kettle_on() ? theme->accent : theme->mark, 4);
    if (k.kettle_boiled())
        for (int i = 0; i < 3; i++)
            arc(p, kx + 12 + i * 12, ky - 10, 4, 7, -1.57f, 1.57f, theme->muted, 2);
    char kt_s[32];
    snprintf(kt_s, sizeof kt_s, "%d degrees", k.kettle_temp());
    p.fit({X(kt.x) + 104, Y(kt.y) + 34, 150, 26}, k.kettle_boiled() ? "boiled" : k.kettle_on() ? "boiling..." : "off",
          style(18, k.kettle_boiled() ? theme->ok : k.kettle_on() ? theme->ink : theme->faint, true), "kit.kettle");
    p.fit({X(kt.x) + 104, Y(kt.y) + 62, 150, 20}, kt_s, style(14, theme->muted), "kit.kettle.t");
    draw_world_status(p);
}

} // namespace ui
