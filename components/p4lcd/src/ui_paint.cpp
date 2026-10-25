// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * p4lcd_ui.hpp: the themes, the Painter, the text-fit check, the band renderer.
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "p4lcd_ui.hpp"

namespace p4lcd::ui {

static const char *TAG = "p4lcd";

static constexpr Color hex(uint32_t v) { return rgb((v >> 16) & 255, (v >> 8) & 255, v & 255); }

const Theme &Theme::dark()
{
    static const Theme t = {.bg = hex(0x111317), .panel = hex(0x1a1d23), .soft = hex(0x22262e), .line = hex(0x2d313a),
                            .mark = hex(0x3c414c), .ink = hex(0xe9ebf0), .muted = hex(0x969cab), .faint = hex(0x6f7583),
                            .accent = hex(0x5d8ef2), .on_accent = hex(0xffffff), .ok = hex(0x36c271),
                            .warn = hex(0xf3b14b), .danger = hex(0xf26b5b)};
    return t;
}

const Theme &Theme::light()
{
    static const Theme t = {.bg = hex(0xf4f5f7), .panel = hex(0xffffff), .soft = hex(0xf0f2f5), .line = hex(0xdfe2e7),
                            .mark = hex(0xc9cdd6), .ink = hex(0x1b1d22), .muted = hex(0x6a6f7a), .faint = hex(0x9aa0ab),
                            .accent = hex(0x2f6fdb), .on_accent = hex(0xffffff), .ok = hex(0x23a35a),
                            .warn = hex(0xe09a1e), .danger = hex(0xd6453a)};
    return t;
}

Color mix(Color a, Color b, float t)
{
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    int r = static_cast<int>(ar + (br - ar) * t + 0.5f), g = static_cast<int>(ag + (bg - ag) * t + 0.5f),
        bl = static_cast<int>(ab + (bb - ab) * t + 0.5f);
    return static_cast<Color>(r << 11 | g << 5 | bl);
}

TextStyle style(float size, Color c, bool bold, Align a, VAlign v)
{
    TextStyle st;
    st.font = bold ? &Font::bold() : &Font::regular();
    st.size = size;
    st.color = c;
    st.align = a;
    st.valign = v;
    return st;
}

/* ---- the painter ---- */

bool Painter::visible(const Rect &r) const
{
    return r.x < a_.x + a_.w && r.x + r.w > a_.x && r.y < a_.y + a_.h && r.y + r.h > a_.y;
}

Painter Painter::clip(const Rect &r)
{
    return Painter(*this, r);
}

Painter::Painter(Painter &parent, const Rect &r) : c_(&view_)
{
    const int x0 = r.x > parent.a_.x ? r.x : parent.a_.x, y0 = r.y > parent.a_.y ? r.y : parent.a_.y;
    const int x1 = r.x + r.w < parent.a_.x + parent.a_.w ? r.x + r.w : parent.a_.x + parent.a_.w;
    const int y1 = r.y + r.h < parent.a_.y + parent.a_.h ? r.y + r.h : parent.a_.y + parent.a_.h;
    if (x1 <= x0 || y1 <= y0 || !parent.c_->pixels()) {
        a_ = {x0, y0, 0, 0}; /* nothing visible: draws nothing */
        return;
    }
    a_ = {x0, y0, x1 - x0, y1 - y0};
    const size_t stride = parent.c_->stride();
    view_.wrap(parent.c_->pixels() + static_cast<size_t>(y0 - parent.a_.y) * stride + (x0 - parent.a_.x), a_.w, a_.h,
               static_cast<int>(stride));
}

void Painter::fill(Color c)
{
    if (a_.w > 0)
        c_->fillRect(0, 0, a_.w, a_.h, c);
}

void Painter::rect(int x, int y, int w, int h, Color c)
{
    if (a_.w > 0)
        c_->fillRect(x - a_.x, y - a_.y, w, h, c);
}

void Painter::frame(int x, int y, int w, int h, Color c, int t)
{
    if (a_.w > 0)
        c_->rect(x - a_.x, y - a_.y, w, h, c, t);
}

void Painter::line(int x0, int y0, int x1, int y1, Color c, int t)
{
    if (a_.w > 0)
        c_->line(x0 - a_.x, y0 - a_.y, x1 - a_.x, y1 - a_.y, c, t);
}

void Painter::blend(int x, int y, Color c, float a)
{
    x -= a_.x;
    y -= a_.y;
    if (x < 0 || y < 0 || x >= a_.w || y >= a_.h || a <= 0)
        return;
    Color *px = c_->pixels() + static_cast<size_t>(y) * c_->stride() + x;
    *px = a >= 1 ? c : mix(*px, c, a);
}

void Painter::fade(int x, int y, int w, int h, Color c, bool from_top)
{
    for (int j = 0; j < h; j++) {
        const float a = from_top ? 1.0f - (j + 0.5f) / h : (j + 0.5f) / h;
        if (y + j < a_.y || y + j >= a_.y + a_.h)
            continue;
        for (int i = 0; i < w; i++)
            blend(x + i, y + j, c, a);
    }
}

void Painter::round(int x, int y, int w, int h, int r, Color c)
{
    if (w <= 0 || h <= 0)
        return;
    r = r < w / 2 ? r : w / 2;
    r = r < h / 2 ? r : h / 2;
    if (r <= 0) {
        rect(x, y, w, h, c);
        return;
    }
    if (!visible({x, y, w, h}))
        return;
    rect(x + r, y, w - 2 * r, h, c);
    rect(x, y + r, r, h - 2 * r, c);
    rect(x + w - r, y + r, r, h - 2 * r, c);
    for (int j = 0; j < r; j++)
        for (int i = 0; i < r; i++) {
            float dx = r - (i + 0.5f), dy = r - (j + 0.5f);
            float a = r - sqrtf(dx * dx + dy * dy) + 0.5f;
            if (a <= 0)
                continue;
            blend(x + i, y + j, c, a);
            blend(x + w - 1 - i, y + j, c, a);
            blend(x + i, y + h - 1 - j, c, a);
            blend(x + w - 1 - i, y + h - 1 - j, c, a);
        }
}

void Painter::roundFrame(int x, int y, int w, int h, int r, Color c, Color inside, int t)
{
    round(x, y, w, h, r, c);
    round(x + t, y + t, w - 2 * t, h - 2 * t, r - t > 0 ? r - t : 0, inside);
}

void Painter::disc(int cx, int cy, int r, Color c)
{
    if (!visible({cx - r - 1, cy - r - 1, 2 * r + 2, 2 * r + 2}))
        return;
    for (int y = cy - r - 1; y <= cy + r + 1; y++)
        for (int x = cx - r - 1; x <= cx + r + 1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            blend(x, y, c, r - sqrtf(dx * dx + dy * dy) + 0.5f);
        }
}

void Painter::dashed(int x0, int x1, int y, Color c, int dash, int gap)
{
    for (int x = x0; x < x1; x += dash + gap)
        rect(x, y, x + dash < x1 ? dash : x1 - x, 1, c);
}

Rect Painter::text(int x, int y, const char *s, const TextStyle &st)
{
    if (a_.w <= 0)
        return Canvas::textBox(x, y, s, st);
    Rect r = c_->text(x - a_.x, y - a_.y, s, st);
    r.x += a_.x;
    r.y += a_.y;
    return r;
}

/* ---- the text-fit check ---- */

static FitStats s_fit;
static uint32_t s_logged[64];
static int s_nlogged;
static void (*s_fit_log)(const char *line);

FitStats fitStats() { return s_fit; }

void resetFitStats()
{
    s_fit = FitStats();
    s_nlogged = 0;
}

void setFitLog(void (*log)(const char *line)) { s_fit_log = log; }

static uint32_t hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ static_cast<uint8_t>(*s++)) * 16777619u;
    return h;
}

static bool first_time(const char *id)
{
    uint32_t h = hash(id ? id : "");
    for (int i = 0; i < s_nlogged; i++)
        if (s_logged[i] == h)
            return false;
    if (s_nlogged < 64)
        s_logged[s_nlogged++] = h;
    return true;
}

Rect Painter::fit(const Rect &box, const char *s, TextStyle st, const char *id, float min_scale)
{
    const float size0 = st.size;
    Rect m = Canvas::textBox(0, 0, s, st);
    while ((m.w > box.w || m.h > box.h) && st.size > size0 * min_scale) {
        st.size -= 1;
        m = Canvas::textBox(0, 0, s, st);
    }
    if (st.size < size0)
        s_fit.shrunk++;
    char cut[256];
    const char *show = s;
    if (m.w > box.w) {
        /* shortened a character at a time (at UTF-8 boundaries), "..." after it */
        size_t n = strlen(s);
        n = n < sizeof cut - 4 ? n : sizeof cut - 4;
        while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80)
            n--;
        for (;;) {
            memcpy(cut, s, n);
            strcpy(cut + n, "...");
            if (n == 0 || Canvas::textBox(0, 0, cut, st).w <= box.w)
                break;
            do
                n--;
            while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80);
        }
        show = cut;
        s_fit.cut++;
        if (first_time(id)) {
            char line[400];
            snprintf(line, sizeof line, "TEXTFIT %s: \"%s\" does not fit %d px at %.0f px; shown as \"%s\"",
                     id ? id : "?", s, box.w, st.size, cut);
            if (s_fit_log)
                s_fit_log(line);
            else
                ESP_LOGW(TAG, "%s", line);
        }
    }
    int x = st.align == Align::LEFT ? box.x : st.align == Align::CENTER ? box.x + box.w / 2 : box.x + box.w;
    int y = st.valign == VAlign::TOP      ? box.y
            : st.valign == VAlign::MIDDLE ? box.y + box.h / 2
            : st.valign == VAlign::BOTTOM ? box.y + box.h
                                          : box.y + box.h * 3 / 4;
    return text(x, y, show, st);
}

/* ---- the band renderer ---- */

bool Renderer::init(int width, int rows)
{
    width_ = width;
    rows_ = rows;
    return band_.create(width, rows);
}

void Renderer::copy(Canvas &target, int x, int y, int w, int h)
{
    Image img = band_.image();
    img.width = w;
    img.height = h;
    img.stride = static_cast<int>(band_.stride());
    target.draw(img, x, y);
}

} // namespace p4lcd::ui
