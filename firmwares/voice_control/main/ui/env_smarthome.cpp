// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The smart home's world (demo::home()): the floor plan, each room's floor warmed by its light and its lamp glowing
 * by the level, its window's blinds, the two doors' padlocks, the thermostat's dial; the music on its own line under
 * the plan, a note in the room it plays in.
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include "demo.hpp"
#include "ui_internal.hpp"

namespace ui {

namespace {

using smarthome::Home;

/* the rooms, in the panel's units (pixels from its top-left) */
constexpr Rect ROOM[smarthome::ROOMS] = {{20, 20, 262, 214}, {290, 20, 278, 300}, {20, 242, 262, 210},
                                         {290, 328, 278, 124}};
/* each room's window on an outer wall: x, y, w (a horizontal one) */
constexpr int WIN[smarthome::ROOMS][3] = {{90, 16, 110}, {380, 16, 120}, {90, 448, 110}, {390, 448, 110}};

int X(int x) { return MAP.x + x; }
int Y(int y) { return MAP.y + y; }

void padlock(Painter &p, int cx, int cy, bool locked, Color bg)
{
    const Color c = locked ? theme->ok : theme->warn;
    if (locked)
        arc(p, cx, cy - 6, 7, 8, 3.14159f, 6.28318f, c, 3);
    else
        arc(p, cx + 6, cy - 9, 7, 8, 3.14159f, 6.28318f, c, 3);
    p.round(cx - 10, cy - 5, 20, 15, 3, c);
    p.disc(cx, cy + 2, 2, bg);
}

/* a note: the mark of the room the music plays in */
void note(Painter &p, int x, int y, Color c)
{
    p.disc(x, y + 12, 5, c);
    p.disc(x + 12, y + 9, 5, c);
    p.line(x + 4, y + 12, x + 4, y, c, 2);
    p.line(x + 16, y + 9, x + 16, y - 3, c, 2);
    p.line(x + 4, y, x + 16, y - 3, c, 3);
}

/* the music, on its own line under the plan: what plays, where, the volume */
void now_playing(Painter &p, int y, const Home &h)
{
    const Color c = theme->accent;
    note(p, X(26), y + 2, c);
    char s[96];
    snprintf(s, sizeof s, "%s: %s, in the %s", h.media(), h.song(), smarthome::room_name(h.music_room()));
    for (char *q = s + strlen(s) - strlen(smarthome::room_name(h.music_room())); *q; q++) /* "in the kitchen" */
        *q = static_cast<char>(*q >= 'A' && *q <= 'Z' ? *q - 'A' + 'a' : *q);
    p.fit({X(52), y, 400, 20}, s, style(14, theme->ink, true, Align::LEFT, VAlign::MIDDLE), "home.song");
    for (int i = 0; i < 10; i++) /* the volume */
        p.round(X(462) + i * 10, y + 16 - i, 7, 4 + i, 2, i < h.volume() ? c : theme->mark);
}

} // namespace

void draw_smarthome(Painter &p)
{
    const Home &h = demo::home();
    world_panel(p);
    const Color fl = floor_color(), wall = theme->mark;
    for (int r = 0; r < smarthome::ROOMS; r++) {
        const Rect &b = ROOM[r];
        const int lv = h.light(r);
        const Color floor = pu::mix(fl, theme->warn, 0.04f + 0.30f * lv / 100.0f);
        p.roundFrame(X(b.x), Y(b.y), b.w, b.h, 12, wall, floor, 3);
        p.fit({X(b.x) + 14, Y(b.y) + 12, 160, 22}, smarthome::room_name(r), style(15, theme->muted, true),
              "home.room");
        /* the lamp */
        /* (the living room's below the thermostat) */
        const int cx = X(b.x + b.w / 2), cy = Y(r == smarthome::LIVING_ROOM ? 206 : b.y + b.h / 2 - 4);
        if (lv) {
            p.disc(cx, cy, 16 + lv / 5, pu::mix(floor, theme->warn, 0.25f));
            p.disc(cx, cy, 12, pu::mix(theme->panel, theme->warn, 0.45f + 0.5f * lv / 100.0f));
        } else {
            p.disc(cx, cy, 12, theme->mark);
            p.disc(cx, cy, 9, floor);
        }
        char lvs[12];
        snprintf(lvs, sizeof lvs, lv ? "%d %%" : "off", lv);
        p.fit({cx - 40, cy + 22, 80, 20}, lvs, style(14, lv ? theme->warn : theme->faint, true, Align::CENTER),
              "home.level");
        /* the window's blinds: the glass, then the slats over the closed part */
        const int wx = X(WIN[r][0]), wy = Y(WIN[r][1]), ww = WIN[r][2];
        p.rect(wx, wy, ww, 8, pu::mix(theme->panel, theme->accent, 0.45f));
        const int closed = static_cast<int>(ww * (1.0f - h.blinds(r)));
        for (int s = 0; s < closed; s += 6)
            p.rect(wx + s, wy, closed - s < 4 ? closed - s : 4, 8, theme->ink);
    }
    /* the doors: the back one in the kitchen's left wall (below its lamp), the front one in the bedroom's bottom wall */
    p.rect(X(16), Y(170), 8, 48, fl);
    padlock(p, X(44), Y(196), h.locked(smarthome::BACK), fl);
    p.fit({X(60), Y(186), 90, 20}, "back door", style(13, theme->muted), "home.door");
    p.rect(X(200), Y(448), 60, 8, fl);
    padlock(p, X(230), Y(426), h.locked(smarthome::FRONT), fl);
    p.fit({X(130), Y(418), 90, 20}, "front door", style(13, theme->muted, false, Align::RIGHT), "home.door");
    /* the thermostat, in the living room */
    const int tx = X(429), ty = Y(100);
    p.disc(tx, ty, 46, theme->mark);
    p.disc(tx, ty, 42, theme->soft);
    const float f = (h.target() - 10) / 20.0f;
    arc(p, tx, ty, 38, 38, 2.4f, 2.4f + 4.6f * f, h.heating() ? theme->warn : h.cooling() ? theme->accent : theme->ok, 4);
    char tt[16], ts[20];
    snprintf(tt, sizeof tt, "%.1f", static_cast<double>(h.inside()));
    snprintf(ts, sizeof ts, "set %d", h.target());
    p.fit({tx - 36, ty - 20, 72, 30}, tt, style(24, theme->ink, true, Align::CENTER, VAlign::MIDDLE), "home.temp");
    p.fit({tx - 36, ty + 10, 72, 20}, ts, style(14, theme->muted, false, Align::CENTER, VAlign::MIDDLE), "home.set");
    p.fit({tx - 70, ty + 50, 140, 20}, h.heating() ? "heating" : h.cooling() ? "cooling" : "steady",
          style(13, h.heating() ? theme->warn : h.cooling() ? theme->accent : theme->ok, true, Align::CENTER),
          "home.heat");
    /* the music: a note in its room's top right corner, the line under the plan */
    if (h.playing()) {
        const Rect &b = ROOM[h.music_room()];
        note(p, X(b.x + b.w) - 34, Y(b.y) + 14, theme->accent);
        now_playing(p, Y(458), h);
    }
    draw_world_status(p);
}

} // namespace ui
