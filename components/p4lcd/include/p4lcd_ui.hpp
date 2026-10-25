// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * p4lcd_ui: a small toolkit for touch apps on p4lcd.
 *
 *     p4lcd::Display lcd;  lcd.open(c);                 // one or two frame buffers
 *     p4lcd::ui::Renderer ren;  ren.init(1024, 64);     // one 1024 x 64 band canvas in PSRAM
 *     const p4lcd::ui::Theme &t = p4lcd::ui::Theme::dark();
 *     ren.render(lcd, {0, 0, 1024, 68}, [&](p4lcd::ui::Painter &p) {
 *         p.fill(t.bg);
 *         p.fit({20, 10, 220, 48}, "Title", p4lcd::ui::style(30, t.ink, true), "title");
 *         p4lcd::ui::button(p, t, ok_button, "ok");
 *     });
 *     if (with one frame buffer) nothing else; with two: lcd.present()
 *
 * The screen is drawn region by region, each region band by band: the draw function paints a band into the band
 * canvas in screen coordinates (the Painter translates and clips), then the PPA copies the band onto the picture.
 * With one frame buffer nothing shows half drawn, and a region can be drawn again alone (a band drawn alone is the
 * same as the same rows of a full drawing when the draw function depends only on the app's state).
 *
 * Text never overflows: Painter::fit makes it smaller (to 75 % by default), then shortens it with "...", counts both
 * cases (fitStats) and logs each cut once per id.
 *
 * Touch: Input merges the panel's events with injected ones (tests, a PC); Pointer applies the rules: a button acts
 * when the finger is lifted inside it, a finger that moves more than 10 px inside a scroll view drags it instead.
 */
#pragma once

#include <cstdint>
#include <cstdlib>
#include "p4lcd.hpp"

namespace p4lcd::ui {

/* ---- colours and text ---- */

struct Theme {
    Color bg;        /* the screen */
    Color panel;     /* cards, panels, rows */
    Color soft;      /* buttons, tiles: a step from bg */
    Color line;      /* borders, tracks, rules */
    Color mark;      /* scrollbars, dashed rules */
    Color ink;       /* text */
    Color muted;     /* labels, secondary text */
    Color faint;     /* text not final yet */
    Color accent;    /* the main action, a selection */
    Color on_accent; /* text on accent, danger, warn and ok */
    Color ok, warn, danger;
    static const Theme &dark();
    static const Theme &light();
};

Color mix(Color a, Color b, float t); /* a at t = 0, b at t = 1, per channel, rounded */

/* a TextStyle in one call: Inter Regular or Bold */
TextStyle style(float size, Color c, bool bold = false, Align a = Align::LEFT, VAlign v = VAlign::TOP);

/* ---- drawing ---- */

/* draws in screen coordinates into a canvas that holds the screen's rectangle `area` (its pixel (0, 0) is area's
 * top-left), clipped to area */
class Painter {
public:
    Painter(Canvas &c, const Rect &area) : c_(&c), a_(area) {}
    Painter(const Painter &) = delete;
    Painter &operator=(const Painter &) = delete;

    Rect area() const { return a_; }
    bool visible(const Rect &r) const; /* r meets the area */
    /* a Painter for the part of this one's area inside r: everything it draws, text included, is cut at r's edges */
    Painter clip(const Rect &r);

    void fill(Color c); /* the whole area */
    void rect(int x, int y, int w, int h, Color c);
    void frame(int x, int y, int w, int h, Color c, int thickness = 1);
    void round(int x, int y, int w, int h, int r, Color c); /* a rounded rectangle, anti-aliased corners */
    void roundFrame(int x, int y, int w, int h, int r, Color c, Color inside, int thickness = 1);
    void disc(int cx, int cy, int r, Color c); /* filled, anti-aliased */
    void line(int x0, int y0, int x1, int y1, Color c, int thickness = 1);
    void dashed(int x0, int x1, int y, Color c, int dash = 6, int gap = 5); /* a horizontal dashed rule */
    /* c over the box, fully at its top (from_top) or bottom, fading to nothing at the other side: a scroll view's edge */
    void fade(int x, int y, int w, int h, Color c, bool from_top);
    /* one pixel blended with c by a (0-1) */
    void blend(int x, int y, Color c, float a);

    /* text as Canvas::text draws it; returns its block in screen coordinates */
    Rect text(int x, int y, const char *utf8, const TextStyle &st);
    /* text inside box with the style's alignment (x at the box's left, centre or right; y at its top, middle, bottom,
     * or for BASELINE at three quarters of its height): made smaller down to min_scale of its size, then shortened
     * with "..." to fit. Returns the block drawn. */
    Rect fit(const Rect &box, const char *utf8, TextStyle st, const char *id, float min_scale = 0.75f);

    Canvas &canvas() { return *c_; } /* the canvas, in its own coordinates (screen minus area's top-left) */

private:
    Painter(Painter &parent, const Rect &r); /* clip() */
    Canvas *c_;
    Rect a_;
    Canvas view_; /* clip()'s window onto the parent's pixels */
};

/* the text-fit record: texts made smaller, texts shortened (all ids, since the start or the last reset) */
struct FitStats {
    int shrunk = 0, cut = 0;
};
FitStats fitStats();
void resetFitStats();
/* where a cut text is reported, once per id (default: ESP_LOGW) */
void setFitLog(void (*log)(const char *line));

/* a region drawn band by band into one canvas, each band copied onto the target by the PPA */
class Renderer {
public:
    /* the band canvas: width x rows in PSRAM (width: the widest region; 1024 x 64 takes 128 KB) */
    bool init(int width, int rows = 64);
    int width() const { return width_; }
    int rows() const { return rows_; }
    /* draw(Painter &) once per band of region; target: the Display, or any Canvas the PPA can write into */
    template <typename F> void render(Canvas &target, const Rect &region, F &&draw)
    {
        for (int y = region.y; y < region.y + region.h; y += rows_) {
            const int h = region.y + region.h - y < rows_ ? region.y + region.h - y : rows_;
            Painter p(band_, {region.x, y, region.w, h});
            draw(p);
            copy(target, region.x, y, region.w, h);
        }
    }

private:
    void copy(Canvas &target, int x, int y, int w, int h);
    Canvas band_;
    int width_ = 0, rows_ = 0;
};

/* ---- widgets: each draws inside its box ---- */

enum class Kind { NORMAL, PRIMARY, DANGER, TAB, ON, OFF };

struct Button {
    Rect r;
    const char *label = "";
    Kind kind = Kind::NORMAL;
    bool enabled = true; /* a disabled button is drawn faded and never takes a press */
    bool pressed = false;
    bool hit(int x, int y) const { return enabled && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
};
void button(Painter &p, const Theme &t, const Button &b, const char *id, float size = 21);

/* n tabs from `first`, each `step` to the right of the last; the selected one NORMAL, the others TAB */
void tabs(Button *out, int n, const char *const *labels, int selected, const Rect &first, int step);

/* a filled pill with its text centred, its width the text's plus 24; returns the width */
int pill(Painter &p, int x, int y, int h, const char *s, Color bg, Color fg, float size);

/* a panel with a border and an optional title in its top row */
void card(Painter &p, const Theme &t, const Rect &r, const char *title = nullptr, const char *id = nullptr);

/* a statistic: a label (wrapped, up to two lines) at the top, the value large at the bottom, its unit after it */
void tile(Painter &p, const Theme &t, const Rect &r, const char *label, const char *value, const char *unit,
          Color value_color, const char *id);

/* a number with its label above it (a row of a grid) */
void numberCard(Painter &p, const Theme &t, const Rect &r, const char *label, const char *value, const char *id);

/* a level: a track, a bar for fraction (0-1, shown from 1 %), a dot at the right; alarm: both in danger */
void meter(Painter &p, const Theme &t, const Rect &r, float fraction, bool alarm);

/* a progress bar: a track, then fraction of it in c (at least its height, so it stays round) */
void progress(Painter &p, const Theme &t, const Rect &r, float fraction, Color c);

/* a setting: a panel row with its label and an optional note under it, text_w wide at the left */
void settingRow(Painter &p, const Theme &t, const Rect &r, const char *label, const char *note, const char *id,
                int text_w = 560);

/* the boxes of a stepper (- value +) ending at `right`, in a row whose top is y; its value drawn centred */
struct Stepper {
    Rect minus, value, plus;
    static Stepper at(int right, int y);
};
void stepperValue(Painter &p, const Theme &t, const Rect &value_box, const char *value, const char *id);

/* the box of the i-th choice from the right (0: the rightmost), 104 x 44, 10 apart, in a row whose top is y */
Rect choiceAt(int right, int y, int i);

/* ---- charts ---- */

struct Series {
    const uint16_t *values; /* count values, the oldest first */
    Color color;
    const char *name;
};

/* bars over time, the series stacked bottom up, the newest at the right */
struct BarChart {
    Rect plot;                    /* the bars' box; the axis labels go in the 54 px left of it */
    int slots = 60;               /* bars across */
    int count = 0;                /* bars with data, in the last slots */
    int top = 0;                  /* the value at the plot's top */
    int grid = 0;                 /* a grid line and an axis label every this much (0: none) */
    const Series *series = nullptr;
    int nseries = 0;
    int ref = -1;                 /* a dashed reference line at this value (-1: none), its label at its right end */
    const char *ref_label = nullptr;
};
void barChart(Painter &p, const Theme &t, const BarChart &c, const char *id);
/* the series' colours and names in a row from (x, y): a 14 px swatch, then its name */
void legend(Painter &p, const Theme &t, int x, int y, const Series *s, int n, const char *id);

/* counts in bins, scaled to the tallest; labels under the axis every label_every bins from the first */
struct Histogram {
    Rect plot;
    const int *bins = nullptr;
    int nbins = 0;
    const char *const *labels = nullptr;
    int nlabels = 0;
    int label_every = 1;
};
void histogram(Painter &p, const Theme &t, const Histogram &h, const char *id);

/* ---- a scroll view ---- */

/* vertical scrolling over content of any height. The app draws the content at y = view.y - offset (clipped to the
 * view) and calls update() with its height first. */
struct Scroll {
    Rect view;                  /* the visible box */
    int back_band = 0;          /* scrolled back, the view's bottom rows that hold a "back to the end" bar (0: none) */
    int offset = 0;             /* the content's row at the view's top */
    bool follow = true;         /* kept at the end as the content grows */

    int viewHeight() const { return view.h - (follow ? 0 : back_band); }
    int maxOffset() const { return content_h > viewHeight() ? content_h - viewHeight() : 0; }
    /* the content's height now: at the end when following, else kept inside the range */
    void update(int content_height);
    bool contains(int x, int y) const { return x >= view.x && x < view.x + view.w && y >= view.y && y < view.y + view.h; }
    /* a finger: grab() at the down, drag(dy) as it moves (dy since the down: no longer following), release() at the
     * up (following again if it let go at the end, within 4 px) */
    void grab() { grab_offset = offset; }
    void drag(int dy);
    void release();
    /* drawing: faded edges where more content lies beyond (fade px); a scrollbar in a 10 px track whose left is x */
    void edges(Painter &p, Color bg, int fade = 22) const;
    void scrollbar(Painter &p, int x, Color c) const;
    /* the back band (when not following and scrollable): a rule, the hint at the left, the button (backButton()) */
    Rect backButton() const { return {view.x + view.w - 200, view.y + view.h - 46, 190, 40}; }
    bool showsBack() const { return !follow && back_band > 0 && maxOffset() > 0; }
    void backBand(Painter &p, const Theme &t, const Button &b, const char *hint, const char *id) const;

    int content_h = 0;
    int grab_offset = 0;
};

/* ---- touch ---- */

/* the panel's touches and injected ones in one queue, in order */
class Input {
public:
    bool open(Touch *touch = nullptr); /* touch: an open panel, or nullptr for injected events only */
    /* from any task */
    void inject(TouchEvent::Type type, int x, int y);
    void tap(int x, int y, uint32_t hold_ms = 120);                                  /* down, hold, up */
    void drag(int x0, int y0, int x1, int y1, int steps = 10, uint32_t step_ms = 40); /* down, moves, up */
    /* the UI's task: the next event, without waiting; false when none */
    bool next(TouchEvent &e);

private:
    Touch *touch_ = nullptr;
    void *queue_ = nullptr;
};

/* the press, drag and tap rules over one finger */
class Pointer {
public:
    enum What { NONE, PRESS, CANCEL, TAP, DRAG, DROP };
    struct Event {
        What what = NONE;
        int id = -1;            /* PRESS, CANCEL, TAP: the button */
        Scroll *view = nullptr; /* CANCEL, DRAG, DROP: the scroll view (it moved) */
    };
    /* hit(x, y): the id of the button that takes a press there (-1: none); view(x, y): the Scroll there or nullptr.
     * DOWN on a button: PRESS. MOVE more than slop px inside a view: CANCEL first if a button was pressed, then
     * DRAG (the view scrolled). UP: TAP when the button pressed at the down is still under the finger, DROP after
     * a drag (the view released). */
    template <typename Hit, typename View> Event feed(const TouchEvent &e, Hit &&hit, View &&view)
    {
        Event out;
        if (e.type == TouchEvent::DOWN) {
            down_ = true;
            dragging_ = false;
            y0_ = e.y;
            pressed_ = hit(e.x, e.y);
            view_ = view(e.x, e.y);
            if (view_)
                view_->grab();
            if (pressed_ >= 0)
                out = {PRESS, pressed_, nullptr};
            return out;
        }
        if (!down_)
            return out;
        if (e.type == TouchEvent::MOVE) {
            const int dy = e.y - y0_;
            if (view_ && (dragging_ || abs(dy) > slop)) {
                if (pressed_ >= 0 && !dragging_) {
                    dragging_ = true;
                    view_->drag(dy);
                    out = {CANCEL, pressed_, view_}; /* the press undone; the view scrolled too */
                    pressed_ = -1;
                    return out;
                }
                dragging_ = true;
                view_->drag(dy);
                out = {DRAG, -1, view_};
            }
            return out;
        }
        down_ = false;
        if (dragging_) {
            view_->release();
            out = {DROP, -1, view_};
        } else if (pressed_ >= 0 && hit(e.x, e.y) == pressed_) {
            out = {TAP, pressed_, nullptr};
        }
        pressed_ = -1;
        return out;
    }
    int pressed() const { return pressed_; } /* the button held down now (draw it pressed), or -1 */
    bool down() const { return down_; }
    int slop = 10;

private:
    bool down_ = false, dragging_ = false;
    int y0_ = 0, pressed_ = -1;
    Scroll *view_ = nullptr;
};

/* ---- measuring and reviewing ---- */

/* the time of each frame that drew something */
struct FrameStats {
    int frames = 0;
    int64_t sum_us = 0;
    int32_t max_us = 0;
    void add(int32_t us)
    {
        frames++;
        sum_us += us;
        max_us = us > max_us ? us : max_us;
    }
    void reset() { *this = FrameStats(); }
    float meanMs() const { return frames ? sum_us / 1000.0f / frames : 0.0f; }
};

/* the picture on screen printed to the console for tools/screen_dump.py (a PNG on the PC): a line
 * "P4LCD-SCREEN <name> <width> <height> <rotation> <bytes> <crc32>", base64 lines of its run-length code (16-bit run,
 * 16-bit colour, little-endian, in the panel's layout), "P4LCD-SCREEN-END". The CRC (zlib's) is over the pixels row by
 * row in that layout, so the PC checks what it decoded. */
bool printScreen(const Display &d, const char *name);

} // namespace p4lcd::ui
