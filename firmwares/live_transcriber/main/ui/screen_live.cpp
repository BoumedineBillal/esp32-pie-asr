// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Live: the controls (Start, Stop, Clear, the level), the transcript (new words lit then fading,
 * the word being written in grey, a caret, a dashed rule at each new line, timestamps if asked; it follows the newest
 * words, a drag reads back), the six tiles. Before the model is in, a card with the load's progress.
 */
#include <algorithm>
#include <cmath>
#include <cstring>
#include "app.hpp"
#include "esp_heap_caps.h"
#include "settings.hpp"
#include "transcript.hpp"
#include "ui_internal.hpp"

namespace ui {

/* the transcript's layout: its words in lines of the text area's width, made again only as far as it changed */
struct TLine {
    int32_t first, end; /* words [first, end) */
    int32_t y;          /* from the content's top */
    uint16_t line;      /* the transcript's line it belongs to */
    uint8_t sep;        /* a new transcript line starts here (a rule above) */
    uint8_t stamp;      /* its first piece (the time is shown when timestamps are on) */
};
static constexpr int MAX_TLINES = 2000, MAX_WORDS = 3000;
static TLine *s_tl;      /* PSRAM */
static uint16_t *s_ww;   /* each word's width, PSRAM */
static int s_ntl, s_lay_words, s_lay_x, s_content_h;
static uint32_t s_lay_gen = 0xffffffff;
static int s_lay_size = -1;
static bool s_lay_ts;
static int64_t s_fade_until;
static int s_fade_y0 = 1 << 30, s_fade_y1 = -1, s_caret_y0, s_caret_y1;

static constexpr int SEP_H = 20;
static float text_px() { return settings::get().text_size == 0 ? 26.0f : settings::get().text_size == 1 ? 32.0f : 40.0f; }
static int line_h() { return static_cast<int>(text_px() * 1.55f + 0.5f); }
static int gutter() { return settings::get().timestamps ? 76 : 0; }
static int text_w() { return R_TEXT.w - gutter() - 14; }
static int width_of(const char *s, const TextStyle &st) { return p4lcd::Canvas::textBox(0, 0, s, st).w; }

bool live_init()
{
    s_tl = static_cast<TLine *>(heap_caps_calloc(MAX_TLINES, sizeof(TLine), MALLOC_CAP_SPIRAM));
    s_ww = static_cast<uint16_t *>(heap_caps_calloc(MAX_WORDS, sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    return s_tl && s_ww;
}

int64_t fade_until() { return s_fade_until; }

void bands_reset()
{
    s_fade_y0 = 1 << 30;
    s_fade_y1 = -1;
}

bool fade_band(int &y0, int &y1)
{
    y0 = s_fade_y0;
    y1 = s_fade_y1;
    return y1 > y0;
}

void caret_band(int &y0, int &y1)
{
    y0 = s_caret_y0;
    y1 = s_caret_y1;
}

static void relayout()
{
    TextStyle st = style(text_px(), theme->ink);
    const int sz = settings::get().text_size;
    transcript::lock();
    const int nw = transcript::words();
    if (transcript::generation() != s_lay_gen || sz != s_lay_size || settings::get().timestamps != s_lay_ts ||
        nw < s_lay_words) {
        s_lay_gen = transcript::generation();
        s_lay_size = sz;
        s_lay_ts = settings::get().timestamps;
        s_ntl = s_lay_words = s_lay_x = s_content_h = 0;
    }
    const int space = width_of("a a", st) - width_of("aa", st), lh = line_h(), tw = text_w();
    for (int i = s_lay_words; i < nw && i < MAX_WORDS && s_ntl < MAX_TLINES; i++) {
        const transcript::Word &w = transcript::word(i);
        int ww = width_of(transcript::text(w), st);
        s_ww[i] = static_cast<uint16_t>(ww);
        TLine *last = s_ntl ? &s_tl[s_ntl - 1] : nullptr;
        int sp = w.space && i ? space : 0;
        if (!last || w.line != last->line) {
            int y = last ? last->y + lh + SEP_H : 0;
            s_tl[s_ntl++] = {i, i, y, w.line, static_cast<uint8_t>(last != nullptr), 1};
            s_lay_x = 0;
            sp = 0;
        } else if (s_lay_x > 0 && s_lay_x + sp + ww > tw) {
            s_tl[s_ntl++] = {i, i, last->y + lh, w.line, 0, 0};
            s_lay_x = 0;
            sp = 0;
        }
        s_tl[s_ntl - 1].end = i + 1;
        s_lay_x += sp + ww;
        s_content_h = s_tl[s_ntl - 1].y + lh;
        int64_t fade = static_cast<int64_t>(w.shown_ms) * 1000 + 600000;
        s_fade_until = std::max(s_fade_until, fade);
    }
    s_lay_words = nw;
    transcript::unlock();
}

/* where the word being written goes: (x, y) in content coordinates, and whether a rule comes before it */
struct TentPlace {
    int x, y;
    bool sep;
    int content_h; /* the content's height with it */
};
static TentPlace tentative_place(const char *tent, int tent_w)
{
    const int lh = line_h();
    transcript::lock();
    const int nw = transcript::words(), tline = transcript::lines() - 1;
    const int last_line = nw ? transcript::word(nw - 1).line : -1;
    transcript::unlock();
    if (!s_ntl)
        return {0, 0, false, lh};
    const TLine &l = s_tl[s_ntl - 1];
    if (tent[0] && tline != last_line) /* the word being written opens a new line */
        return {0, l.y + lh + SEP_H, true, l.y + 2 * lh + SEP_H};
    TextStyle st = style(text_px(), theme->ink);
    int space = tent[0] ? width_of("a a", st) - width_of("aa", st) : 0;
    if (s_lay_x + space + tent_w + 18 <= text_w())
        return {s_lay_x + space, l.y, false, s_content_h};
    return {0, l.y + lh, false, l.y + 2 * lh};
}

/* the model is not there yet: a card in the middle of the text area */
static void draw_loading(Painter &p, asr::State st)
{
    const Rect card = {R_TEXT.x + 120, R_TEXT.y + 40, R_TEXT.w - 240, 220};
    p.round(card.x, card.y, card.w, card.h, 14, theme->soft);
    const char *title = st == asr::FAILED          ? "Something went wrong"
                        : st == asr::PREPARING     ? "Preparing the model"
                        : st == asr::LOADING_MODEL ? "Loading the model"
                                                   : "Waiting for the model";
    p.fit({card.x + 30, card.y + 26, card.w - 60, 40}, title, style(30, theme->ink, true), "load.title");
    char msg[200];
    if (st == asr::FAILED)
        snprintf(msg, sizeof msg, "%s", asr::failure());
    else if (st == asr::PREPARING)
        snprintf(msg, sizeof msg, "The 2- and 4-bit weights are being set up (about 1 s).");
    else if (st == asr::LOADING_MODEL && !strcmp(asr::model_source(), "USB"))
        snprintf(msg, sizeof msg, "The weights (25.4 MB) are coming over USB from the PC.");
    else if (st == asr::LOADING_MODEL)
        snprintf(msg, sizeof msg, "The weights (25.4 MB) are being read from %s.", asr::model_source());
    else if (*asr::waiting_reason())
        snprintf(msg, sizeof msg, "%s Then restart the board.", asr::waiting_reason());
    else
        snprintf(msg, sizeof msg, "Put %s on the microSD card and restart the board.", app::MODEL_FILE);
    TextStyle ms = style(21, theme->muted);
    ms.wrap = card.w - 60;
    p.fit({card.x + 30, card.y + 76, card.w - 60, 80}, msg, ms, "load.msg");
    const int bx = card.x + 30, by = card.y + 160, bw = card.w - 60;
    const float f = st == asr::LOADING_MODEL ? asr::load_progress() : st == asr::PREPARING ? 1.0f : 0.0f;
    pu::progress(p, *theme, {bx, by, bw, 14}, f, st == asr::PREPARING ? theme->ok : theme->accent);
    char pc[32];
    snprintf(pc, sizeof pc, "%.0f %%", f * 100);
    p.fit({bx, by + 20, bw, 30}, pc, style(19, theme->muted, false, Align::RIGHT), "load.pc");
}

void draw_text_area(Painter &p, int64_t now)
{
    p.fill(theme->panel);
    const asr::State st = asr::state();
    if (st == asr::WAITING_MODEL || st == asr::LOADING_MODEL || st == asr::PREPARING || st == asr::FAILED) {
        draw_loading(p, st);
        return;
    }
    relayout();
    const int lh = line_h(), base = static_cast<int>(lh * 0.68f), g = gutter();
    const settings::Settings &se = settings::get();
    char tent[400];
    transcript::lock();
    snprintf(tent, sizeof tent, "%s", se.show_tentative ? transcript::tentative() : "");
    const int nw = transcript::words();
    transcript::unlock();
    TextStyle ink = style(text_px(), theme->ink, false, Align::LEFT, VAlign::BASELINE);
    TextStyle tst = ink;
    tst.color = theme->faint;
    int tent_w = tent[0] ? width_of(tent, tst) : 0;
    TentPlace tp = tentative_place(tent, tent_w);
    live_view.update(tp.content_h);
    const int off = live_view.offset;
    const Rect band = p.area();
    if (!nw && !tent[0]) {
        p.fit({R_TEXT.x, R_TEXT.y + 6, R_TEXT.w, 40},
              st == asr::LISTENING ? "Listening: speak English into the board's microphone."
                                   : "Press \"Start microphone\" and speak.",
              style(22, theme->muted), "text.hint");
    }
    /* the pieces that meet this band */
    const int top = off + (band.y - R_TEXT.y) - lh - SEP_H, bottom = off + (band.y - R_TEXT.y) + band.h;
    int lo = 0, hi = s_ntl;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (s_tl[mid].y < top)
            lo = mid + 1;
        else
            hi = mid;
    }
    transcript::lock();
    for (int k = lo; k < s_ntl && s_tl[k].y < bottom; k++) {
        const TLine &l = s_tl[k];
        const int sy = R_TEXT.y + l.y - off;
        if (l.sep)
            p.dashed(R_TEXT.x, R_TEXT.x + R_TEXT.w - 14, sy - SEP_H / 2, theme->mark);
        if (g && l.stamp && l.line < transcript::lines()) {
            char ts[16];
            fmt_time(ts, sizeof ts, transcript::line(l.line).start_ms);
            p.text(R_TEXT.x, sy + base, ts, style(text_px() * 0.55f, theme->muted, false, Align::LEFT, VAlign::BASELINE));
        }
        int x = R_TEXT.x + g;
        for (int i = l.first; i < l.end && i < transcript::words(); i++) {
            const transcript::Word &w = transcript::word(i);
            if (i > l.first && w.space)
                x += width_of("a a", ink) - width_of("aa", ink);
            int64_t age = now - static_cast<int64_t>(w.shown_ms) * 1000;
            if (age < 600000) { /* as the page's 0.6 s fade from the accent */
                Color bg = pu::mix(theme->panel, theme->accent, 0.30f * (1.0f - age / 600000.0f));
                p.round(x - 4, sy + 4, s_ww[i] + 8, lh - 8, 6, bg);
                s_fade_y0 = std::min(s_fade_y0, sy);
                s_fade_y1 = std::max(s_fade_y1, sy + lh);
            }
            p.text(x, sy + base, transcript::text(w), ink);
            x += s_ww[i];
        }
    }
    transcript::unlock();
    /* the word being written, then the caret */
    const int ty = R_TEXT.y + tp.y - off;
    if (tp.sep)
        p.dashed(R_TEXT.x, R_TEXT.x + R_TEXT.w - 14, ty - SEP_H / 2, theme->mark);
    int cx = R_TEXT.x + g + tp.x;
    if (tent[0]) {
        p.text(cx, ty + base, tent, tst);
        cx += tent_w;
    }
    s_caret_y0 = ty;
    s_caret_y1 = ty + lh;
    if (listening_now() && (now / 500000) % 2 == 0)
        p.round(cx + 4, ty + base - static_cast<int>(text_px() * 0.82f), 9, static_cast<int>(text_px() * 1.0f), 2,
                theme->accent);
    /* the view's edges where more text lies beyond them; scrolled back, the band with "Back to live" */
    live_view.edges(p, theme->panel);
    live_view.backBand(p, *theme, buttons[B_BACK_LIVE], "Reading back: the newest words are below.", "text.back");
}

void render_text_band(int y0, int y1, int64_t now)
{
    y0 = std::max(y0, R_TEXT.y);
    y1 = std::min(y1, R_TEXT.y + R_TEXT.h);
    if (y1 > y0)
        render({R_TEXT.x, y0, R_TEXT.w, y1 - y0}, [now](Painter &p) { draw_text_area(p, now); });
}

void draw_scrollbar(Painter &p)
{
    p.fill(theme->panel);
    const asr::State st = asr::state();
    if (st == asr::IDLE || st == asr::LISTENING || st == asr::FINISHING)
        live_view.scrollbar(p, R_SCROLL.x, theme->mark);
}

void draw_meter(Painter &p)
{
    const asr::Level l = asr::level();
    const float lin = powf(10.0f, l.rms_db / 20.0f); /* the page's sqrt(rms) * 220 % */
    pu::meter(p, *theme, R_METER, std::min(1.0f, sqrtf(lin) * 2.2f), l.clipping);
}

void draw_controls(Painter &p)
{
    p.fill(theme->bg);
    pu::button(p, *theme, buttons[B_START], "btn.start");
    pu::button(p, *theme, buttons[B_STOP], "btn.stop");
    pu::button(p, *theme, buttons[B_CLEAR], "btn.clear");
    if (p.visible(R_METER))
        draw_meter(p);
}

static void tile(Painter &p, int i, const char *label, const char *value, const char *unit, Color vc, const char *id)
{
    pu::tile(p, *theme, {16 + i * 167, 480, 157, 110}, label, value, unit, vc, id);
}

void draw_tiles(Painter &p)
{
    p.fill(theme->bg);
    const asr::Stats s = asr::stats();
    transcript::lock();
    const transcript::Delays d = transcript::delays();
    const int lines = transcript::lines();
    const int32_t line_start = transcript::line(lines - 1).start_ms;
    const int nw = transcript::words();
    const int32_t last_end = nw ? transcript::word(nw - 1).end_ms : 0;
    transcript::unlock();
    char v[32];
    snprintf(v, sizeof v, "%.1f", s.listening_ms / 1000.0);
    tile(p, 0, "listening", v, "s", theme->ink, "tile.listening");
    if (d.n)
        snprintf(v, sizeof v, "%.1f", d.last / 1000.0);
    else
        snprintf(v, sizeof v, "-");
    tile(p, 1, "last word final after", v, "s", theme->ink, "tile.last");
    if (d.n)
        snprintf(v, sizeof v, "%.1f", d.median / 1000.0);
    else
        snprintf(v, sizeof v, "-");
    tile(p, 2, "median word delay", v, "s", theme->ink, "tile.median");
    snprintf(v, sizeof v, "%.1f", s.behind_ms / 1000.0);
    tile(p, 3, "behind live", v, "s", s.behind_ms > 3000 ? theme->danger : theme->ink, "tile.behind");
    if (s.processed_ms > 0)
        snprintf(v, sizeof v, "%.2f", s.x_real_time);
    else
        snprintf(v, sizeof v, "-");
    tile(p, 4, "compute / audio", v, "x real time", theme->ink, "tile.rtf");
    char u[24];
    snprintf(v, sizeof v, "#%d", lines);
    snprintf(u, sizeof u, "%.0f s", nw ? std::max<int32_t>(0, last_end - line_start) / 1000.0 : 0.0);
    tile(p, 5, "line", v, u, theme->ink, "tile.line");
}

} // namespace ui
