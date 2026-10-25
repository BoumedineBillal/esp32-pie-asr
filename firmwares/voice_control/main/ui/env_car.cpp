// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The car cabin's world (demo::car()): the car from above (its windows open or closed, the heated seats, the beams,
 * the windscreen's defrost), and four cards: the climate, the route, the media, the battery.
 */
#include <cstdio>
#include "demo.hpp"
#include "ui_internal.hpp"

namespace ui {

namespace {

using car::Car;

int X(int x) { return MAP.x + x; }
int Y(int y) { return MAP.y + y; }

/* a side window: open from the top down (the open part dark, framed in amber) */
void side_window(Painter &p, int x, int y, int h, float open)
{
    const int o = static_cast<int>(h * open);
    p.rect(X(x), Y(y), 9, h, pu::mix(theme->panel, theme->accent, 0.45f));
    if (o > 0) {
        p.rect(X(x), Y(y), 9, o, theme->bg);
        p.roundFrame(X(x) - 1, Y(y) - 1, 11, o + 2, 2, theme->warn, theme->bg, 1);
    }
}

void seat(Painter &p, int x, int y, int w, int h, int heat)
{
    const Color c = heat ? pu::mix(theme->soft, theme->warn, 0.5f) : theme->soft;
    p.round(X(x), Y(y), w, h, 12, c);
    p.round(X(x) + 6, Y(y) + h - 16, w - 12, 10, 5, pu::mix(c, theme->ink, 0.08f));
    if (heat)
        for (int i = 0; i < 3; i++) {
            const int lx = X(x) + w / 4 + i * w / 4;
            p.line(lx, Y(y) + 14, lx + 5, Y(y) + 24, theme->warn, 2);
            p.line(lx + 5, Y(y) + 24, lx, Y(y) + 34, theme->warn, 2);
        }
}

void card(Painter &p, const Rect &r, const char *title)
{
    p.roundFrame(X(r.x), Y(r.y), r.w, r.h, 10, theme->line, theme->soft, 1);
    p.fit({X(r.x) + 12, Y(r.y) + 8, 120, 20}, title, style(14, theme->muted, true), "car.card");
}

void bar(Painter &p, int x, int y, int w, float f, Color c)
{
    p.round(x, y, w, 8, 4, theme->mark);
    if (f > 0)
        p.round(x, y, static_cast<int>(w * (f > 1 ? 1 : f)) < 8 ? 8 : static_cast<int>(w * (f > 1 ? 1 : f)), 8, 4, c);
}

} // namespace

void draw_car(Painter &p)
{
    const Car &c = demo::car();
    world_panel(p);
    /* the beams, then the car */
    if (c.headlights())
        for (int k = 0; k < 2; k++) {
            const int hx = X(k ? 196 : 84);
            for (int i = -2; i <= 2; i++)
                p.line(hx, Y(74), hx + i * 12, Y(18), pu::mix(theme->panel, theme->warn, 0.5f), 3);
        }
    p.roundFrame(X(40), Y(70), 200, 380, 56, theme->mark, theme->soft, 2);
    p.round(X(68), Y(74), 32, 8, 3, c.headlights() ? theme->warn : theme->mark);
    p.round(X(180), Y(74), 32, 8, 3, c.headlights() ? theme->warn : theme->mark);
    /* the windscreen and its defrost, the rear window */
    p.round(X(62), Y(124), 156, 50, 16, pu::mix(theme->panel, theme->accent, 0.45f));
    if (c.defrosting())
        for (int i = 0; i < 4; i++)
            p.line(X(76), Y(134 + i * 10), X(204), Y(134 + i * 10), theme->warn, 2);
    p.round(X(70), Y(400), 140, 32, 12, pu::mix(theme->panel, theme->accent, 0.45f));
    /* the side windows: driver (front left), passenger (front right), rear left, rear right */
    side_window(p, 40, 186, 90, c.window(car::DRIVER));
    side_window(p, 231, 186, 90, c.window(car::PASSENGER));
    side_window(p, 40, 296, 80, c.window(car::REAR_LEFT));
    side_window(p, 231, 296, 80, c.window(car::REAR_RIGHT));
    /* the wheel and the seats */
    p.disc(X(97), Y(194), 15, theme->ink);
    p.disc(X(97), Y(194), 11, theme->soft);
    seat(p, 64, 214, 66, 80, c.seat(car::DRIVER));
    seat(p, 150, 214, 66, 80, c.seat(car::PASSENGER));
    seat(p, 64, 306, 152, 66, 0);
    p.fit({X(40), Y(454), 200, 20}, "driver on the left", style(13, theme->faint, false, Align::CENTER),
          "car.side");
    /* the climate */
    const Rect cl = {264, 16, 308, 132};
    card(p, cl, "Climate");
    const char *zone[2] = {"driver", "passenger"};
    for (int z = 0; z < 2; z++) {
        const int zx = X(cl.x) + 14 + z * 148;
        char t[16], s[24];
        snprintf(t, sizeof t, "%.1f", static_cast<double>(c.cabin(z)));
        snprintf(s, sizeof s, "%s, set %d", zone[z], c.target(z));
        p.fit({zx, Y(cl.y) + 32, 130, 36}, t, style(30, theme->ink, true), "car.temp");
        p.fit({zx, Y(cl.y) + 70, 140, 20}, s, style(13, theme->muted), "car.set");
    }
    p.fit({X(cl.x) + 14, Y(cl.y) + 100, 40, 20}, "fan", style(13, theme->muted), "car.fan");
    for (int i = 0; i < 5; i++)
        p.round(X(cl.x) + 48 + i * 16, Y(cl.y) + 112 - i * 3, 11, 8 + i * 3, 2, i < c.fan() ? theme->accent : theme->mark);
    if (c.defrosting())
        pu::pill(p, X(cl.x) + 200, Y(cl.y) + 98, 24, "defrost", pu::mix(theme->panel, theme->warn, 0.3f), theme->warn,
                 13);
    /* the route */
    const Rect rt = {264, 156, 308, 100};
    card(p, rt, "Route");
    if (c.destination() >= 0) {
        char d[48];
        snprintf(d, sizeof d, "%.1f km, %d min", static_cast<double>(c.distance()), c.eta_minutes());
        p.fit({X(rt.x) + 14, Y(rt.y) + 30, 280, 28}, car::destination_name(c.destination()),
              style(22, theme->ink, true), "car.dest");
        p.fit({X(rt.x) + 14, Y(rt.y) + 60, 280, 20}, d, style(15, theme->muted), "car.km");
        pu::pill(p, X(rt.x) + 220, Y(rt.y) + 6, 24, "driving", pu::mix(theme->panel, theme->accent, 0.3f),
                 theme->accent, 13);
    } else {
        p.fit({X(rt.x) + 14, Y(rt.y) + 34, 280, 26}, "No route: parked", style(18, theme->faint), "car.noroute");
    }
    /* the media */
    const Rect md = {264, 264, 308, 100};
    card(p, md, "Media");
    p.fit({X(md.x) + 14, Y(md.y) + 30, 200, 26}, c.playing() ? c.track() : "paused",
          style(19, c.playing() ? theme->ink : theme->faint, true), "car.track");
    p.fit({X(md.x) + 14, Y(md.y) + 58, 200, 20}, c.media(), style(14, theme->muted), "car.media");
    for (int i = 0; i < 10; i++)
        p.round(X(md.x) + 200 + i * 9, Y(md.y) + 84 - i * 2, 6, 6 + i * 2, 2, i < c.volume() ? theme->accent : theme->mark);
    /* the battery */
    const Rect bt = {264, 372, 308, 80};
    card(p, bt, "Battery");
    char b[32];
    snprintf(b, sizeof b, "%d %%, %d km", c.battery(), c.range_km());
    p.fit({X(bt.x) + 14, Y(bt.y) + 30, 280, 22}, b, style(18, theme->ink, true), "car.batt");
    bar(p, X(bt.x) + 14, Y(bt.y) + 58, 280, c.battery() / 100.0f, theme->ok);
    draw_world_status(p);
}

} // namespace ui
