// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Drawing (p4lcd.hpp): shapes and text into the picture being drawn, in picture coordinates (the rotation applied
 * here). Everything is a clipped rectangle fill underneath, so every shape is exact and gap-free.
 */
#include <algorithm>
#include <cstring>
#include "font.hpp"
#include "p4lcd.hpp"

namespace p4lcd {

/* the largest integer whose square is <= v */
static int isqrt(int v)
{
    if (v <= 0)
        return 0;
    int r = 0, bit = 1 << 30;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

void Canvas::fillRect(int x, int y, int w, int h, Color c)
{
    if (!buf_)
        return;
    /* clip in picture coordinates */
    int x1 = std::min(x + w, width()), y1 = std::min(y + h, height());
    x = std::max(x, 0);
    y = std::max(y, 0);
    if (x >= x1 || y >= y1)
        return;
    w = x1 - x;
    h = y1 - y;
    /* the same rectangle in the panel's layout */
    int px, py, pw, ph;
    switch (rotation_) {
    case 90: px = pw_ - y - h; py = x; pw = h; ph = w; break;
    case 180: px = pw_ - x - w; py = ph_ - y - h; pw = w; ph = h; break;
    case 270: px = y; py = ph_ - x - w; pw = h; ph = w; break;
    default: px = x; py = y; pw = w; ph = h; break;
    }
    Color *row = buf_ + (size_t)py * stride_ + px;
    for (int r = 0; r < ph; r++, row += stride_)
        std::fill_n(row, pw, c);
}

void Canvas::fill(Color c) { fillRect(0, 0, width(), height(), c); }

void Canvas::pixel(int x, int y, Color c) { fillRect(x, y, 1, 1, c); }

void Canvas::span(int y, int x0, int x1, Color c) { fillRect(x0, y, x1 - x0 + 1, 1, c); }

void Canvas::rect(int x, int y, int w, int h, Color c, int t)
{
    if (w <= 0 || h <= 0 || t <= 0)
        return;
    if (2 * t >= w || 2 * t >= h) {
        fillRect(x, y, w, h, c);
        return;
    }
    fillRect(x, y, w, t, c);
    fillRect(x, y + h - t, w, t, c);
    fillRect(x, y + t, t, h - 2 * t, c);
    fillRect(x + w - t, y + t, t, h - 2 * t, c);
}

void Canvas::line(int x0, int y0, int x1, int y1, Color c, int t)
{
    if (t <= 0)
        return;
    /* straight lines as one rectangle */
    if (y0 == y1) {
        fillRect(std::min(x0, x1), y0 - (t - 1) / 2, std::abs(x1 - x0) + 1, t, c);
        return;
    }
    if (x0 == x1) {
        fillRect(x0 - (t - 1) / 2, std::min(y0, y1), t, std::abs(y1 - y0) + 1, c);
        return;
    }
    /* Bresenham, a t x t square at each point; both ends included */
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, o = (t - 1) / 2;
    for (;;) {
        fillRect(x0 - o, y0 - o, t, t, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* a ring of thickness t (t >= r: a disc): per row, the outer circle's span minus the inner circle's */
void Canvas::circle(int cx, int cy, int r, Color c, int t)
{
    if (r < 0 || t <= 0)
        return;
    int ri = r - t;
    for (int dy = -r; dy <= r; dy++) {
        int xo = isqrt(r * r - dy * dy);
        if (ri >= 0 && std::abs(dy) <= ri) {
            int xi = isqrt(ri * ri - dy * dy);
            span(cy + dy, cx - xo, cx - xi - 1, c);
            span(cy + dy, cx + xi + 1, cx + xo, c);
        } else {
            span(cy + dy, cx - xo, cx + xo, c);
        }
    }
}

void Canvas::fillCircle(int cx, int cy, int r, Color c) { circle(cx, cy, r, c, r + 1); }

int Canvas::glyphs(int x, int y, const char *s, Color fg, const Color *bg, int scale)
{
    if (scale < 1)
        scale = 1;
    int cx = x, cy = y, widest = 0;
    for (; *s; s++) {
        if (*s == '\n') {
            widest = std::max(widest, cx - x);
            cx = x;
            cy += detail::FONT_H * scale;
            continue;
        }
        unsigned ch = static_cast<unsigned char>(*s);
        if (ch < 32 || ch > 126)
            ch = '?';
        if (bg)
            fillRect(cx, cy, detail::FONT_W * scale, detail::FONT_H * scale, *bg);
        const uint8_t *g = detail::FONT_8X16[ch - 32];
        for (int r = 0; r < detail::FONT_H; r++) {
            /* each run of set pixels in the row as one rectangle */
            for (int b = 0; b < detail::FONT_W;) {
                if (!(g[r] & (0x80 >> b))) {
                    b++;
                    continue;
                }
                int e = b;
                while (e < detail::FONT_W && (g[r] & (0x80 >> e)))
                    e++;
                fillRect(cx + b * scale, cy + r * scale, (e - b) * scale, scale, fg);
                b = e;
            }
        }
        cx += detail::FONT_W * scale;
    }
    return std::max(widest, cx - x);
}

int Canvas::text(int x, int y, const char *s, Color fg, int scale) { return glyphs(x, y, s, fg, nullptr, scale); }

int Canvas::text(int x, int y, const char *s, Color fg, Color bg, int scale) { return glyphs(x, y, s, fg, &bg, scale); }

int Canvas::textWidth(const char *s, int scale)
{
    int n = 0, widest = 0;
    for (; *s; s++) {
        if (*s == '\n') {
            widest = std::max(widest, n);
            n = 0;
        } else {
            n++;
        }
    }
    return std::max(widest, n) * detail::FONT_W * std::max(scale, 1);
}

int Canvas::textHeight(const char *s, int scale)
{
    int lines = 1;
    for (; *s; s++)
        lines += *s == '\n';
    return lines * detail::FONT_H * std::max(scale, 1);
}

} // namespace p4lcd
