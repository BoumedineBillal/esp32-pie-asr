// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The icons, from the Painter's shapes: rounded rectangles, discs, thick lines (an arc: short segments).
 */
#include <cmath>
#include "ui_internal.hpp"

namespace ui {

void arc(Painter &p, float cx, float cy, float rx, float ry, float a0, float a1, Color c, int t)
{
    constexpr int N = 10;
    float px = cx + rx * std::cos(a0), py = cy + ry * std::sin(a0);
    for (int i = 1; i <= N; i++) {
        const float a = a0 + (a1 - a0) * i / N;
        const float x = cx + rx * std::cos(a), y = cy + ry * std::sin(a);
        p.line(static_cast<int>(px), static_cast<int>(py), static_cast<int>(x), static_cast<int>(y), c, t);
        px = x;
        py = y;
    }
}

void icon_home(Painter &p, int cx, int cy, Color c, Color bg)
{
    p.line(cx - 12, cy - 1, cx, cy - 12, c, 3);
    p.line(cx, cy - 12, cx + 12, cy - 1, c, 3);
    p.round(cx - 8, cy - 3, 16, 14, 2, c);
    p.round(cx - 2, cy + 3, 5, 8, 1, bg);
}

void icon_mic(Painter &p, int cx, int cy, Color c, Color bg, float k)
{
    (void)bg;
    const int w = static_cast<int>(10 * k), h = static_cast<int>(17 * k);
    p.round(cx - w / 2, cy - static_cast<int>(12 * k), w, h, w / 2, c);
    const int t = k > 1.5f ? 4 : 2;
    arc(p, cx, cy - 1 * k, 9 * k, 8 * k, 0, 3.14159f, c, t);
    p.line(cx, static_cast<int>(cy + 7 * k), cx, static_cast<int>(cy + 12 * k), c, t);
    p.line(static_cast<int>(cx - 5 * k), static_cast<int>(cy + 12 * k), static_cast<int>(cx + 5 * k),
           static_cast<int>(cy + 12 * k), c, t);
}

void icon_transcribe(Painter &p, int cx, int cy, Color c, Color bg)
{
    icon_mic(p, cx - 34, cy, c, bg, 2.3f);
    const int lens[4] = {62, 50, 58, 36};
    for (int i = 0; i < 4; i++)
        p.round(cx + 2, cy - 30 + i * 18, lens[i], 8, 4, c);
}

void icon_control(Painter &p, int cx, int cy, Color c, Color bg)
{
    const int hs[5] = {14, 30, 46, 30, 14};
    for (int i = 0; i < 5; i++)
        p.round(cx - 64 + i * 11, cy - hs[i] / 2, 6, hs[i], 3, c);
    p.line(cx - 6, cy, cx + 16, cy, c, 4);
    p.line(cx + 10, cy - 7, cx + 18, cy, c, 4);
    p.line(cx + 10, cy + 7, cx + 18, cy, c, 4);
    for (int i = 0; i < 2; i++) {
        const int y = cy - 16 + i * 32;
        p.roundFrame(cx + 30, y - 11, 44, 22, 11, c, bg, 3);
        p.disc(i == 0 ? cx + 63 : cx + 41, y, 7, c);
    }
}

void icon_vacuum(Painter &p, int cx, int cy, Color c, Color bg)
{
    p.disc(cx, cy, 16, c);
    p.disc(cx, cy, 13, bg);
    p.disc(cx, cy, 4, c);
    p.line(cx, cy, cx + 11, cy, c, 3);
}

void icon_house(Painter &p, int cx, int cy, Color c, Color bg)
{
    p.line(cx - 17, cy - 2, cx, cy - 17, c, 3);
    p.line(cx, cy - 17, cx + 17, cy - 2, c, 3);
    p.round(cx - 12, cy - 5, 24, 20, 2, c);
    p.disc(cx, cy + 3, 5, bg); /* a light */
    p.disc(cx, cy + 3, 2, c);
}

void icon_car(Painter &p, int cx, int cy, Color c, Color bg)
{
    p.round(cx - 18, cy - 4, 36, 14, 5, c);
    p.round(cx - 11, cy - 13, 22, 12, 4, c);
    p.round(cx - 8, cy - 10, 16, 7, 2, bg);
    p.disc(cx - 10, cy + 11, 5, c);
    p.disc(cx + 10, cy + 11, 5, c);
    p.disc(cx - 10, cy + 11, 2, bg);
    p.disc(cx + 10, cy + 11, 2, bg);
}

void icon_factory(Painter &p, int cx, int cy, Color c, Color bg)
{
    p.round(cx - 18, cy - 2, 36, 18, 2, c);
    p.line(cx - 18, cy - 2, cx - 8, cy - 10, c, 3);
    p.line(cx - 8, cy - 10, cx - 8, cy - 2, c, 3);
    p.line(cx - 8, cy - 2, cx + 2, cy - 10, c, 3);
    p.rect(cx + 8, cy - 18, 6, 16, c); /* the chimney */
    for (int i = 0; i < 3; i++)
        p.rect(cx - 13 + i * 10, cy + 5, 5, 5, bg);
}

void icon_kitchen(Painter &p, int cx, int cy, Color c, Color bg)
{
    p.round(cx - 16, cy - 6, 32, 20, 5, c); /* a pot */
    p.rect(cx - 21, cy - 4, 6, 3, c);
    p.rect(cx + 15, cy - 4, 6, 3, c);
    p.round(cx - 18, cy - 10, 36, 5, 2, c);
    p.rect(cx - 3, cy - 14, 6, 4, c);
    arc(p, cx - 6, cy - 22, 3, 5, -1.57f, 1.57f, c, 2); /* steam */
    arc(p, cx + 5, cy - 22, 3, 5, -1.57f, 1.57f, c, 2);
    p.round(cx - 10, cy + 2, 20, 4, 2, bg);
}

void icon_presenter(Painter &p, int cx, int cy, Color c, Color bg)
{
    p.round(cx - 19, cy - 15, 38, 24, 3, c);
    p.rect(cx - 15, cy - 11, 30, 16, bg);
    p.rect(cx - 11, cy - 3, 5, 6, c);
    p.rect(cx - 3, cy - 7, 5, 10, c);
    p.rect(cx + 5, cy - 9, 5, 12, c);
    p.line(cx, cy + 9, cx, cy + 15, c, 3);
    p.line(cx - 8, cy + 16, cx + 8, cy + 16, c, 3);
}

} // namespace ui
