// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The presenter's world (demo::deck()): the show's state and the talk's timer, the current slide (it slides in when
 * it changes; zoomed; black when blanked), the deck's ten thumbnails. While a slide comes in, only the slide is drawn
 * again.
 */
#include <cstdio>
#include "demo.hpp"
#include "ui_internal.hpp"

namespace ui {

namespace {

using presenter::Deck;

constexpr Rect SLIDE = {16, 56, 556, 313};   /* 16:9, the panel's units */

int X(int x) { return MAP.x + x; }
int Y(int y) { return MAP.y + y; }

/* slide i's content in the box (x, y, w, h), its sizes times k (the zoom, toward the title's corner: it stays in
 * view) */
void slide_content(Painter &p, int i, int x, int y, int w, int h, float k)
{
    const Color paper = pu::mix(theme->ink, theme->panel, 0.05f), dark = theme->bg;
    p.rect(x, y, w, h, paper);
    const int ax = w / 12, ay = h / 10;
    auto sx = [&](int u) { return x + ax + static_cast<int>((u - ax) * k); };
    auto sy = [&](int v) { return y + ay + static_cast<int>((v - ay) * k); };
    if (i == 0 || i == presenter::SLIDES - 1) { /* the title, the end */
        p.fit({sx(20), sy(h / 2 - 40), static_cast<int>((w - 40) * k), static_cast<int>(60 * k)},
              presenter::slide_title(i), style(34 * k, dark, true, Align::CENTER, VAlign::MIDDLE), "pres.big");
        if (i == 0)
            p.fit({sx(20), sy(h / 2 + 24), static_cast<int>((w - 40) * k), static_cast<int>(30 * k)},
                  "esp32-pie-asr on the ESP32-P4", style(18 * k, theme->faint, false, Align::CENTER),
                  "pres.sub");
        return;
    }
    p.fit({sx(28), sy(22), static_cast<int>((w - 56) * k), static_cast<int>(40 * k)}, presenter::slide_title(i),
          style(28 * k, dark, true), "pres.title");
    p.rect(sx(28), sy(70), static_cast<int>(80 * k), static_cast<int>(4 * k) > 1 ? static_cast<int>(4 * k) : 1,
           theme->accent);
    for (int b = 0; b < 3; b++) { /* the bullets */
        const int len = 140 + ((i * 37 + b * 53) % 140);
        p.disc(sx(36), sy(104 + b * 44), static_cast<int>(5 * k), theme->accent);
        p.round(sx(52), sy(98 + b * 44), static_cast<int>(len * k), static_cast<int>(12 * k), static_cast<int>(6 * k),
                pu::mix(paper, dark, 0.35f));
    }
    if (i % 2) /* a chart on every other slide */
        for (int b = 0; b < 5; b++) {
            const int bh = 30 + ((i * 29 + b * 41) % 110);
            p.rect(sx(w - 200 + b * 34), sy(h - 40 - bh), static_cast<int>(24 * k), static_cast<int>(bh * k),
                   b == 4 ? theme->accent : pu::mix(paper, theme->accent, 0.45f));
        }
}

} // namespace

bool presenter_moving(Rect &box)
{
    box = {X(SLIDE.x), Y(SLIDE.y), SLIDE.w, SLIDE.h};
    return demo::deck().change() < 1.0f;
}

void draw_presenter(Painter &p)
{
    const Deck &d = demo::deck();
    world_panel(p);
    /* the state, the slide's number, the zoom, the timer */
    pu::pill(p, X(16), Y(16), 28, d.presenting() ? "presenting" : "not presenting",
             d.presenting() ? theme->danger : theme->soft, d.presenting() ? theme->on_accent : theme->muted, 14);
    char n[24], tm[48];
    snprintf(n, sizeof n, "slide %d of %d", d.slide() + 1, presenter::SLIDES);
    p.fit({X(170), Y(16), 140, 28}, n, style(16, theme->ink, true, Align::LEFT, VAlign::MIDDLE), "pres.n");
    if (d.zoom() > 100) {
        char z[24];
        snprintf(z, sizeof z, "zoom %d %%", d.zoom());
        pu::pill(p, X(300), Y(16), 28, z, pu::mix(theme->panel, theme->accent, 0.3f), theme->accent, 14);
    }
    snprintf(tm, sizeof tm, "%s %d:%02d of 20:00", d.timing() ? "timer" : "timer off", d.elapsed() / 60,
             d.elapsed() % 60);
    p.fit({X(392), Y(16), 180, 28}, tm,
          style(15, d.timing() ? theme->ink : theme->faint, true, Align::RIGHT, VAlign::MIDDLE), "pres.timer");
    /* the slide in its frame: black when blanked; coming in from the right while it changes */
    p.roundFrame(X(SLIDE.x) - 3, Y(SLIDE.y) - 3, SLIDE.w + 6, SLIDE.h + 6, 4,
                 d.presenting() ? theme->accent : theme->line, theme->bg, 2);
    Painter c = p.clip({X(SLIDE.x), Y(SLIDE.y), SLIDE.w, SLIDE.h});
    if (d.blank()) {
        c.fit({X(SLIDE.x), Y(SLIDE.y) + SLIDE.h / 2 - 15, SLIDE.w, 30}, "screen blanked",
              style(18, theme->faint, false, Align::CENTER, VAlign::MIDDLE), "pres.blank");
    } else {
        const float e = 1.0f - d.change();
        const int shift = static_cast<int>(SLIDE.w * e * e);
        if (shift > 0)
            slide_content(c, d.previous(), X(SLIDE.x) - (SLIDE.w - shift), Y(SLIDE.y), SLIDE.w, SLIDE.h, 1.0f);
        slide_content(c, d.slide(), X(SLIDE.x) + shift, Y(SLIDE.y), SLIDE.w, SLIDE.h, d.zoom() / 100.0f);
    }
    /* the thumbnails */
    for (int i = 0; i < presenter::SLIDES; i++) {
        const int tx = X(16) + i * 56, ty = Y(384);
        const bool cur = i == d.slide();
        p.roundFrame(tx - 2, ty - 2, 54, 33, 4, cur ? theme->accent : theme->line, theme->panel, cur ? 2 : 1);
        p.rect(tx, ty, 50, 29, pu::mix(theme->ink, theme->panel, cur ? 0.1f : 0.45f));
        p.rect(tx + 5, ty + 5, 30, 4, theme->bg);
        p.rect(tx + 5, ty + 13, 22, 3, theme->faint);
        char num[4];
        snprintf(num, sizeof num, "%d", i + 1);
        p.fit({tx, ty + 34, 50, 16}, num, style(12, cur ? theme->accent : theme->faint, true, Align::CENTER),
              "pres.thumb");
    }
    /* the talk's time */
    const float f = d.elapsed() / static_cast<float>(presenter::TALK_SECONDS);
    p.round(X(16), Y(446), 556, 8, 4, theme->mark);
    if (f > 0)
        p.round(X(16), Y(446), static_cast<int>(556 * (f > 1 ? 1 : f)) < 8 ? 8 : static_cast<int>(556 * (f > 1 ? 1 : f)),
                8, 4, f > 1 ? theme->danger : theme->accent);
    draw_world_status(p);
}

} // namespace ui
