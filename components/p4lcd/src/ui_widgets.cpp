// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * p4lcd_ui.hpp: the widgets and the charts. Each draws inside its box; the text in them goes through Painter::fit.
 */
#include <cstdio>
#include <cstring>
#include "p4lcd_ui.hpp"

namespace p4lcd::ui {

/* an id with a suffix, for the text-fit log */
static const char *sub(char *buf, size_t n, const char *id, const char *suffix)
{
    snprintf(buf, n, "%s.%s", id ? id : "?", suffix);
    return buf;
}

void button(Painter &p, const Theme &t, const Button &b, const char *id, float size)
{
    if (!p.visible(b.r))
        return;
    Color bg, fg;
    switch (b.kind) {
    case Kind::PRIMARY: bg = t.accent, fg = t.on_accent; break;
    case Kind::DANGER: bg = t.danger, fg = t.on_accent; break;
    case Kind::TAB: bg = t.bg, fg = t.muted; break;
    case Kind::ON: bg = t.accent, fg = t.on_accent; break;
    case Kind::OFF: bg = t.soft, fg = t.muted; break;
    default: bg = t.soft, fg = t.ink; break;
    }
    if (!b.enabled) { /* faded into the background (as a web page's opacity 0.45) */
        bg = mix(t.bg, bg, 0.45f);
        fg = mix(t.bg, fg, 0.45f);
    }
    if (b.pressed)
        bg = mix(bg, t.ink, 0.18f);
    if (b.kind == Kind::NORMAL || b.kind == Kind::OFF)
        p.roundFrame(b.r.x, b.r.y, b.r.w, b.r.h, 9, t.line, bg);
    else if (b.kind != Kind::TAB || b.pressed)
        p.round(b.r.x, b.r.y, b.r.w, b.r.h, 9, bg);
    p.fit({b.r.x + 6, b.r.y + 2, b.r.w - 12, b.r.h - 4}, b.label, style(size, fg, true, Align::CENTER, VAlign::MIDDLE),
          id);
}

void tabs(Button *out, int n, const char *const *labels, int selected, const Rect &first, int step)
{
    for (int i = 0; i < n; i++) {
        out[i] = Button();
        out[i].r = {first.x + i * step, first.y, first.w, first.h};
        out[i].label = labels[i];
        out[i].kind = i == selected ? Kind::NORMAL : Kind::TAB;
    }
}

int pill(Painter &p, int x, int y, int h, const char *s, Color bg, Color fg, float size)
{
    TextStyle st = style(size, fg, true, Align::CENTER, VAlign::MIDDLE);
    int w = Canvas::textBox(0, 0, s, st).w + 24;
    p.round(x, y, w, h, h / 2, bg);
    p.text(x + w / 2, y + h / 2, s, st);
    return w;
}

void card(Painter &p, const Theme &t, const Rect &r, const char *title, const char *id)
{
    p.roundFrame(r.x, r.y, r.w, r.h, 12, t.line, t.panel);
    if (title)
        p.fit({r.x + 16, r.y + 12, r.w - 32, 30}, title, style(19, t.ink, true), id);
}

void tile(Painter &p, const Theme &t, const Rect &r, const char *label, const char *value, const char *unit,
          Color value_color, const char *id)
{
    char b[48];
    p.round(r.x, r.y, r.w, r.h, 12, t.soft);
    TextStyle ls = style(16, t.muted);
    ls.wrap = r.w - 24;
    p.fit({r.x + 12, r.y + 10, r.w - 24, 40}, label, ls, sub(b, sizeof b, id, "label"));
    const int vy = r.y + r.h - 52;
    Rect vr = p.fit({r.x + 12, vy, r.w - 24, 42}, value, style(30, value_color, true, Align::LEFT, VAlign::BASELINE), id);
    const int ux = vr.x + vr.w + 6, uw = r.x + r.w - 10 - ux;
    if (unit && unit[0] && uw > 10)
        p.fit({ux, vy, uw, 42}, unit, style(15, t.muted, false, Align::LEFT, VAlign::BASELINE),
              sub(b, sizeof b, id, "unit"), 0.8f);
}

void numberCard(Painter &p, const Theme &t, const Rect &r, const char *label, const char *value, const char *id)
{
    char b[48];
    p.round(r.x, r.y, r.w, r.h, 10, t.soft);
    p.fit({r.x + 12, r.y + 8, r.w - 24, 22}, label, style(15, t.muted), sub(b, sizeof b, id, "label"));
    p.fit({r.x + 12, r.y + 32, r.w - 24, 36}, value, style(24, t.ink, true, Align::LEFT, VAlign::MIDDLE), id);
}

void meter(Painter &p, const Theme &t, const Rect &r, float fraction, bool alarm)
{
    p.rect(r.x, r.y, r.w, r.h, t.bg);
    const Rect m = {r.x, r.y + (r.h - 10) / 2, r.w - 34, 10};
    p.round(m.x, m.y, m.w, m.h, 5, t.line);
    fraction = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
    if (fraction > 0.01f)
        p.round(m.x, m.y, m.w * fraction > 10 ? static_cast<int>(m.w * fraction) : 10, m.h, 5, alarm ? t.danger : t.ok);
    p.disc(r.x + r.w - 12, r.y + r.h / 2, 7, alarm ? t.danger : t.line);
}

void progress(Painter &p, const Theme &t, const Rect &r, float fraction, Color c)
{
    p.round(r.x, r.y, r.w, r.h, r.h / 2, t.line);
    fraction = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
    if (fraction > 0)
        p.round(r.x, r.y, r.w * fraction > r.h ? static_cast<int>(r.w * fraction) : r.h, r.h, r.h / 2, c);
}

void settingRow(Painter &p, const Theme &t, const Rect &r, const char *label, const char *note, const char *id,
                int text_w)
{
    char b[48];
    p.round(r.x, r.y, r.w, r.h, 10, t.panel);
    p.fit({r.x + 20, r.y + 6, text_w, 26}, label, style(20, t.ink, true), sub(b, sizeof b, id, "label"));
    if (note)
        p.fit({r.x + 20, r.y + 32, text_w, 22}, note, style(15, t.muted), sub(b, sizeof b, id, "note"));
}

Stepper Stepper::at(int right, int y)
{
    return {{right - 240, y + 8, 60, 44}, {right - 176, y + 8, 112, 44}, {right - 60, y + 8, 60, 44}};
}

void stepperValue(Painter &p, const Theme &t, const Rect &box, const char *value, const char *id)
{
    p.fit(box, value, style(22, t.ink, true, Align::CENTER, VAlign::MIDDLE), id);
}

Rect choiceAt(int right, int y, int i) { return {right - 102 - i * 114, y + 8, 104, 44}; }

/* ---- charts ---- */

void barChart(Painter &p, const Theme &t, const BarChart &c, const char *id)
{
    char b[48];
    const Rect &pl = c.plot;
    if (c.top <= 0)
        return;
    if (c.grid > 0) {
        const TextStyle ax = style(14, t.muted, false, Align::RIGHT, VAlign::MIDDLE);
        for (int v = 0; v <= c.top; v += c.grid) {
            const int y = pl.y + pl.h - pl.h * v / c.top;
            p.rect(pl.x, y, pl.w, 1, t.line);
            char s[16];
            snprintf(s, sizeof s, "%d", v);
            p.fit({pl.x - 54, y - 10, 48, 20}, s, ax, sub(b, sizeof b, id, "axis"));
        }
    }
    const int bw = pl.w / c.slots;
    for (int i = 0; i < c.count; i++) {
        const int x = pl.x + (c.slots - c.count + i) * bw, y = pl.y + pl.h;
        int acc = 0;
        for (int k = 0; k < c.nseries; k++) {
            const int h = pl.h * c.series[k].values[i] / c.top;
            p.rect(x + 1, y - acc - h, bw - 2, h, c.series[k].color);
            acc += h;
        }
    }
    if (c.ref >= 0) {
        const int y = pl.y + pl.h - pl.h * c.ref / c.top;
        p.dashed(pl.x, pl.x + pl.w, y, t.danger, 8, 5);
        if (c.ref_label)
            p.fit({pl.x + pl.w - 230, y - 24, 230, 20}, c.ref_label, style(14, t.danger, true, Align::RIGHT),
                  sub(b, sizeof b, id, "ref"));
    }
}

void legend(Painter &p, const Theme &t, int x, int y, const Series *s, int n, const char *id)
{
    for (int i = 0; i < n; i++) {
        p.round(x, y, 14, 14, 3, s[i].color);
        Rect r = p.fit({x + 20, y - 4, 150, 22}, s[i].name, style(15, t.muted), id);
        x = r.x + r.w + 24;
    }
}

void histogram(Painter &p, const Theme &t, const Histogram &h, const char *id)
{
    const Rect &pl = h.plot;
    if (h.nbins <= 0)
        return;
    int top = 1;
    for (int b = 0; b < h.nbins; b++)
        top = h.bins[b] > top ? h.bins[b] : top;
    const int bw = pl.w / h.nbins;
    for (int b = 0; b < h.nbins; b++) {
        const int bh = pl.h * h.bins[b] / top;
        p.rect(pl.x + b * bw + 2, pl.y + pl.h - bh, bw - 4, bh, t.accent);
    }
    p.rect(pl.x, pl.y + pl.h, pl.w, 1, t.line);
    for (int k = 0; k < h.nlabels; k++)
        p.fit({pl.x + k * h.label_every * bw - 10, pl.y + pl.h + 6, 30, 20}, h.labels[k],
              style(14, t.muted, false, Align::CENTER), id);
}

} // namespace p4lcd::ui
