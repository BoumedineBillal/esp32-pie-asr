// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * ui.hpp: the frame loop, the buttons, the touch, the pages. Each region is drawn by its page's function
 * (screen_*.cpp) with p4lcd's Renderer (band by band into one canvas, each band copied onto the single frame buffer
 * by the PPA), only when something it shows changed. Touch: p4lcd::ui::Pointer's rules (a button acts on the finger's
 * release inside it; the transcript and History scroll with a drag).
 *
 * The demos: the selected demo's world is simulated every frame (wherever the user is); on its page what moves (the
 * robot, the belt, a slide coming in) is drawn again in its box, the whole world when its version changes. Phrase list
 * 1 + the demo's index is the one in use while a demo's pages are open, list 0 otherwise.
 */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "demo.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.hpp"
#include "transcript.hpp"
#include "ui.hpp"
#include "ui_internal.hpp"

namespace ui {

static const char *const SCREEN_NAMES[SCREENS] = {"Live", "Stats", "History", "Phrases", "Settings", "About"};
static const char *const PAGE_IDS[PAGES] = {"live", "stats", "history", "phrases", "settings", "about",
                                            "home", "demos", "demo",    "rules"};

Button buttons[B_COUNT];
const pu::Theme *theme = &pu::Theme::dark();
Screen current = HOME;
pu::Scroll live_view = {R_TEXT, 52};
pu::Scroll hist_view = {R_HIST_TEXT, 0};
bool saving;
uint32_t saved_gen = 0xffffffff;

static p4lcd::Display s_lcd;
static p4lcd::Touch s_touch;
static pu::Renderer s_ren;
static pu::Input s_in;
static pu::Pointer s_ptr;
static pu::FrameStats s_frames;
static uint32_t s_save_seen;
static TaskHandle_t s_ui_task;

p4lcd::Display &display() { return s_lcd; }
pu::Renderer &renderer() { return s_ren; }

/* the debug link's screenshots and page changes, from the main task */
static std::atomic<void (*)(int, int, int, const uint16_t *)> s_shot_fn{nullptr};
static std::atomic<int> s_screen_req{-1}, s_demo_req{-1};

/* what must be drawn again */
static bool d_all = true, d_header, d_controls, d_meter, d_text, d_scroll, d_tiles, d_body, d_hist;
static bool d_map, d_heard, d_actions, d_say, d_rules;

/* ---- the buttons ---- */
static Button make(const Rect &r, const char *label, Kind k, bool enabled = true)
{
    Button b;
    b.r = r;
    b.label = label;
    b.kind = k;
    b.enabled = enabled;
    return b;
}

static Kind toggle(bool on) { return on ? Kind::ON : Kind::OFF; }

static bool model_ready()
{
    const asr::State st = asr::state();
    return st != asr::WAITING_MODEL && st != asr::LOADING_MODEL && st != asr::PREPARING && st != asr::FAILED;
}

static void layout_buttons()
{
    const asr::State st = asr::state();
    const bool ready = model_ready();
    pu::tabs(&buttons[B_TAB0], SCREENS, SCREEN_NAMES, current, {400, 14, 98, 40}, 102);
    buttons[B_START] = make({16, 78, 170, 46}, "Start microphone", Kind::PRIMARY, ready && st == asr::IDLE);
    buttons[B_STOP] = make({196, 78, 110, 46}, "Stop", Kind::DANGER, st == asr::LISTENING);
    buttons[B_CLEAR] = make({316, 78, 104, 46}, "Clear", Kind::NORMAL,
                            ready && st == asr::IDLE && transcript::words() > 0);
    buttons[B_BACK_LIVE] = make(live_view.backButton(), "Back to live", Kind::PRIMARY);
    buttons[B_SAVE] = make({808, 74, 200, 44}, "Save to SD card", Kind::NORMAL, !saving && transcript::words() > 0);
    const settings::Settings &se = settings::get();
    const pu::Stepper g = pu::Stepper::at(ROW_RIGHT, row_y(0)), wt = pu::Stepper::at(ROW_RIGHT, row_y(1)),
                      pa = pu::Stepper::at(ROW_RIGHT, row_y(2));
    buttons[B_GAIN_M] = make(g.minus, "-", Kind::NORMAL, se.gain_db > 12);
    buttons[B_GAIN_P] = make(g.plus, "+", Kind::NORMAL, se.gain_db < 66);
    buttons[B_WT_M] = make(wt.minus, "-", Kind::NORMAL, se.word_timeout_s > 0.25f);
    buttons[B_WT_P] = make(wt.plus, "+", Kind::NORMAL, se.word_timeout_s < 2.0f);
    buttons[B_PAUSE_M] = make(pa.minus, "-", Kind::NORMAL, se.pause_s > 0.65f);
    buttons[B_PAUSE_P] = make(pa.plus, "+", Kind::NORMAL, se.pause_s < 5.0f);
    buttons[B_SIZE_S] = make(pu::choiceAt(ROW_RIGHT, row_y(3), 2), "Small", toggle(se.text_size == 0));
    buttons[B_SIZE_M] = make(pu::choiceAt(ROW_RIGHT, row_y(3), 1), "Medium", toggle(se.text_size == 1));
    buttons[B_SIZE_L] = make(pu::choiceAt(ROW_RIGHT, row_y(3), 0), "Large", toggle(se.text_size == 2));
    buttons[B_TENT] = make(pu::choiceAt(ROW_RIGHT, row_y(4), 0), se.show_tentative ? "On" : "Off",
                           toggle(se.show_tentative));
    buttons[B_TS] = make(pu::choiceAt(ROW_RIGHT, row_y(5), 0), se.timestamps ? "On" : "Off", toggle(se.timestamps));
    buttons[B_FOLLOW] = make(pu::choiceAt(ROW_RIGHT, row_y(6), 0), se.follow ? "On" : "Off", toggle(se.follow));
    buttons[B_PH_ALL] = make(pu::choiceAt(ROW_RIGHT, 82, 0), phrases::on() ? "On" : "Off", toggle(phrases::on()));
    for (int j = 0; j < phrases::COLUMN; j++) { /* (faded and still while all of them are off) */
        const bool w = j < phrases::words(), s = j < phrases::sentences(), on = phrases::on();
        buttons[B_PH_WORD0 + j] =
            make({PH_WORDS.x + 12, ph_row_y(j), PH_WORDS.w - 24, 36}, w ? phrases::item(phrases::word(j)).shown : "",
                 toggle(w && phrases::enabled(phrases::word(j))), w && on);
        buttons[B_PH_SENT0 + j] =
            make({PH_SENTS.x + 12, ph_row_y(j), PH_SENTS.w - 24, 36},
                 s ? phrases::item(phrases::sentence(j)).shown : "", toggle(s && phrases::enabled(phrases::sentence(j))),
                 s && on);
    }
    /* the pages' own (drawn by their pages: the label is not used for the tiles and cards) */
    buttons[B_HOME] = make({16, 13, 44, 42}, "", Kind::NORMAL);
    buttons[B_TILE_LIVE] = make({52, 178, 440, 330}, "", Kind::NORMAL);
    buttons[B_TILE_DEMOS] = make({532, 178, 440, 330}, "", Kind::NORMAL);
    for (int i = 0; i < demo::COUNT; i++)
        buttons[B_CARD0 + i] = make(card_rect(i), "", Kind::NORMAL);
    buttons[B_DEMO_MIC] = make({W - 16 - 150, 13, 150, 42}, listening_now() ? "Stop mic" : "Start mic",
                               listening_now() ? Kind::DANGER : Kind::PRIMARY,
                               ready && (st == asr::IDLE || st == asr::LISTENING));
    buttons[B_DEMO_RESET] = make({W - 16 - 150 - 10 - 110, 13, 110, 42}, "Reset", Kind::NORMAL);
    buttons[B_DEMO_RULES] = make({W - 16 - 150 - 10 - 110 - 10 - 100, 13, 100, 42}, "Rules", Kind::NORMAL);
    buttons[B_RULES_BACK] = make({W - 16 - 132, 13, 132, 42}, "Back", Kind::NORMAL);
    const int cw = (SAY.w - 28 - 8) / 2;
    for (int i = 0; i < SAY_CHIPS; i++)
        buttons[B_SAY0 + i] = make({SAY.x + 14 + (i % 2) * (cw + 8), SAY.y + 46 + (i / 2) * 34, cw, 30},
                                   demo_page().say[i], Kind::NORMAL);
    if (s_ptr.pressed() >= 0)
        buttons[s_ptr.pressed()].pressed = true;
}

/* the buttons the page shown has */
static bool on_screen(int id)
{
    if (transcriber(current) && (id <= B_TAB_LAST || id == B_HOME))
        return true;
    switch (current) {
    case LIVE: return id == B_START || id == B_STOP || id == B_CLEAR || (id == B_BACK_LIVE && !live_view.follow);
    case HISTORY: return id == B_SAVE;
    case PHRASES:
        return id == B_PH_ALL || (id >= B_PH_WORD0 && id < B_PH_WORD0 + phrases::words()) ||
               (id >= B_PH_SENT0 && id < B_PH_SENT0 + phrases::sentences());
    case SETTINGS: return id >= B_GAIN_M && id <= B_FOLLOW;
    case HOME: return id == B_TILE_LIVE || id == B_TILE_DEMOS;
    case DEMOS: return id == B_HOME || (id >= B_CARD0 && id <= B_CARD_LAST);
    case DEMO:
        return id == B_HOME || id == B_DEMO_MIC || id == B_DEMO_RESET || id == B_DEMO_RULES ||
               (id >= B_SAY0 && id <= B_SAY_LAST);
    case RULES: return id == B_HOME || id == B_RULES_BACK;
    default: return false;
    }
}

/* ---- the frame ---- */
void state_pill(char *s, size_t n, Color &bg, Color &fg)
{
    bg = theme->soft;
    fg = theme->muted;
    switch (asr::state()) {
    case asr::LISTENING: snprintf(s, n, "listening"), bg = theme->danger, fg = theme->on_accent; break;
    case asr::FINISHING: snprintf(s, n, "finishing"), bg = theme->warn, fg = theme->on_accent; break;
    case asr::LOADING_MODEL:
        snprintf(s, n, "loading %.0f %%", asr::load_progress() * 100), bg = theme->warn, fg = theme->on_accent;
        break;
    case asr::PREPARING: snprintf(s, n, "preparing"), bg = theme->warn, fg = theme->on_accent; break;
    case asr::WAITING_MODEL: snprintf(s, n, "no model"); break;
    case asr::FAILED: snprintf(s, n, "failed"), bg = theme->danger, fg = theme->on_accent; break;
    default: snprintf(s, n, "idle"); break;
    }
}

/* a page's header: the home button, an optional "crumbs /", the title, the state's pill */
static Rect page_title(Painter &p, const char *crumbs, const char *title)
{
    p.fill(theme->bg);
    const Button &h = buttons[B_HOME];
    p.roundFrame(h.r.x, h.r.y, h.r.w, h.r.h, 10, h.pressed ? theme->accent : theme->line, theme->soft, 1);
    icon_home(p, h.r.x + h.r.w / 2, h.r.y + h.r.h / 2 + 1, theme->ink, theme->soft);
    int x = 74;
    if (crumbs)
        x += p.fit({x, 10, 220, 48}, crumbs, style(16, theme->muted, false, Align::LEFT, VAlign::MIDDLE),
                   "header.crumbs").w + 8;
    const Rect r = p.fit({x, 10, 300, 48}, title, style(28, theme->ink, true, Align::LEFT, VAlign::MIDDLE),
                         "header.title");
    char s[48];
    Color bg, fg;
    state_pill(s, sizeof s, bg, fg);
    pu::pill(p, r.x + r.w + 14, 20, 30, s, bg, fg, 16);
    return r;
}

static void draw_header(Painter &p)
{
    switch (current) {
    case HOME: draw_home_header(p); return;
    case DEMOS: page_title(p, nullptr, "Voice control"); break;
    case DEMO:
        page_title(p, "Voice control  /", demo_page().title);
        pu::button(p, *theme, buttons[B_DEMO_RULES], "btn.demo", 18);
        pu::button(p, *theme, buttons[B_DEMO_RESET], "btn.demo", 18);
        pu::button(p, *theme, buttons[B_DEMO_MIC], "btn.demo", 18);
        break;
    case RULES: {
        char crumbs[48];
        snprintf(crumbs, sizeof crumbs, "%s  /", demo_page().title);
        page_title(p, crumbs, "Rules");
        pu::button(p, *theme, buttons[B_RULES_BACK], "btn.demo", 18);
        break;
    }
    default: /* the transcriber: its tabs */
        page_title(p, nullptr, "Transcriber");
        for (int i = 0; i < SCREENS; i++)
            pu::button(p, *theme, buttons[B_TAB0 + i], PAGE_IDS[i], 19);
        break;
    }
    p.rect(0, 67, W, 1, theme->line);
}

static void draw_all()
{
    layout_buttons();
    render(R_HEADER, [](Painter &p) { draw_header(p); });
    switch (current) {
    case LIVE:
        render(R_CONTROLS, [](Painter &p) { draw_controls(p); });
        render(R_PANEL, [](Painter &p) {
            p.fill(theme->bg);
            p.round(PANEL.x, PANEL.y, PANEL.w, PANEL.h, 12, theme->line);
            p.round(PANEL.x + 1, PANEL.y + 1, PANEL.w - 2, PANEL.h - 2, 11, theme->panel);
        });
        render(R_TEXT, [](Painter &p) { draw_text_area(p, esp_timer_get_time()); });
        render(R_SCROLL, [](Painter &p) { draw_scrollbar(p); });
        render(R_TILES, [](Painter &p) { draw_tiles(p); });
        break;
    case STATS:
        render(R_STATS_TOP, [](Painter &p) { draw_stats_top(p); });
        render(R_STATS_GRID, [](Painter &p) { draw_stats_grid(p); });
        break;
    case HISTORY:
        render(R_HIST_BAR, [](Painter &p) { draw_hist_bar(p); });
        render(R_HIST_LIST, [](Painter &p) { draw_hist_list(p); });
        break;
    case PHRASES: render(R_BODY, [](Painter &p) { draw_phrases(p); }); break;
    case SETTINGS: render(R_BODY, [](Painter &p) { draw_settings(p); }); break;
    case ABOUT: render(R_BODY, [](Painter &p) { draw_about(p); }); break;
    case HOME: render(R_BODY, [](Painter &p) { draw_home(p); }); break;
    case DEMOS: render(R_BODY, [](Painter &p) { draw_demos(p); }); break;
    case DEMO:
        render(R_MAP, [](Painter &p) { draw_world(p); });
        render(R_HEARD, [](Painter &p) { draw_heard(p); });
        render(R_ACTIONS, [](Painter &p) { draw_actions(p); });
        render(R_SAY, [](Painter &p) { draw_say(p); });
        break;
    default: render(R_BODY, [](Painter &p) { draw_rules(p); }); break;
    }
}

static void apply_settings()
{
    const settings::Settings &se = settings::get();
    transcript::set_rules(se.word_timeout_s, se.pause_s);
    asr::set_gain(se.gain_db);
    live_view.follow = se.follow;
}

/* to page s: the demo opened or left */
static void go(Screen s)
{
    if (in_demo(s) && !in_demo(current))
        demo::enter();
    else if (!in_demo(s) && in_demo(current))
        demo::leave();
    current = s;
    d_all = true;
}

/* a tap on button id */
static void press(int id)
{
    settings::Settings &se = settings::get();
    bool changed = false;
    if (id >= B_TAB0 && id <= B_TAB_LAST) {
        go(static_cast<Screen>(id - B_TAB0));
        return;
    }
    if (id == B_PH_ALL || (id >= B_PH_WORD0 && id <= B_PH_LAST)) { /* applied by the service between chunks */
        if (id == B_PH_ALL) {
            phrases::set_on(!phrases::on());
        } else {
            const int j = id < B_PH_SENT0 ? id - B_PH_WORD0 : id - B_PH_SENT0;
            const int i = id < B_PH_SENT0 ? phrases::word(j) : phrases::sentence(j);
            phrases::set_enabled(i, !phrases::enabled(i));
        }
        se.phrases_on = phrases::on();
        se.phrases_off = phrases::off_mask();
        settings::save();
        d_all = true;
        return;
    }
    if (id >= B_SAY0 && id <= B_SAY_LAST) {
        demo::say(demo_page().say[id - B_SAY0]);
        d_say = true;
        return;
    }
    if (id >= B_CARD0 && id <= B_CARD_LAST) {
        demo::select(id - B_CARD0);
        go(DEMO);
        return;
    }
    switch (id) {
    case B_HOME: go(HOME); return;
    case B_TILE_LIVE: go(LIVE); return;
    case B_TILE_DEMOS: go(DEMOS); return;
    case B_DEMO_RULES: rules_view.offset = 0; go(RULES); return;
    case B_RULES_BACK: go(DEMO); return;
    case B_DEMO_RESET: demo::reset(); d_all = true; return;
    case B_DEMO_MIC:
        if (listening_now())
            asr::request_stop();
        else
            asr::request_start();
        d_header = true;
        return;
    case B_START: asr::request_start(); live_view.follow = true; break;
    case B_STOP: asr::request_stop(); break;
    case B_CLEAR: asr::request_clear(); break;
    case B_BACK_LIVE: live_view.follow = true; break;
    case B_SAVE: saving = true; s_save_seen = asr::save_count(); asr::request_save(); d_hist = true; break;
    case B_GAIN_M: se.gain_db = std::max(12.0f, se.gain_db - 3); changed = true; break;
    case B_GAIN_P: se.gain_db = std::min(66.0f, se.gain_db + 3); changed = true; break;
    case B_WT_M: se.word_timeout_s = std::max(0.2f, roundf((se.word_timeout_s - 0.1f) * 10) / 10); changed = true; break;
    case B_WT_P: se.word_timeout_s = std::min(2.0f, roundf((se.word_timeout_s + 0.1f) * 10) / 10); changed = true; break;
    case B_PAUSE_M: se.pause_s = std::max(0.6f, roundf((se.pause_s - 0.1f) * 10) / 10); changed = true; break;
    case B_PAUSE_P: se.pause_s = std::min(5.0f, roundf((se.pause_s + 0.1f) * 10) / 10); changed = true; break;
    case B_SIZE_S: se.text_size = 0; changed = true; break;
    case B_SIZE_M: se.text_size = 1; changed = true; break;
    case B_SIZE_L: se.text_size = 2; changed = true; break;
    case B_TENT: se.show_tentative = !se.show_tentative; changed = true; break;
    case B_TS: se.timestamps = !se.timestamps; changed = true; break;
    case B_FOLLOW: se.follow = !se.follow; changed = true; break;
    default: break;
    }
    if (changed) {
        settings::save();
        apply_settings();
        d_all = true;
    }
    d_controls = d_header = true;
}

static void on_touch(const p4lcd::TouchEvent &e)
{
    layout_buttons();
    auto hit = [](int x, int y) {
        for (int id = 0; id < B_COUNT; id++)
            if (on_screen(id) && buttons[id].hit(x, y))
                return id;
        return -1;
    };
    auto view = [](int x, int y) -> pu::Scroll * {
        if (current == LIVE && live_view.contains(x, y))
            return &live_view;
        if (current == HISTORY && y >= R_HIST_TEXT.y && y < R_HIST_TEXT.y + R_HIST_TEXT.h)
            return &hist_view;
        if (current == RULES && rules_view.contains(x, y))
            return &rules_view;
        return nullptr;
    };
    const pu::Pointer::Event ev = s_ptr.feed(e, hit, view);
    if (ev.view == &live_view)
        d_text = d_scroll = true;
    else if (ev.view == &hist_view)
        d_hist = true;
    else if (ev.view == &rules_view)
        d_rules = true;
    if (ev.what == pu::Pointer::PRESS || ev.what == pu::Pointer::CANCEL || e.type == p4lcd::TouchEvent::UP) {
        /* a pressed button drawn again: the whole page, except on a demo's (its header or Say only) */
        if (current == DEMO) {
            const int id = ev.id >= 0 ? ev.id : s_ptr.pressed();
            if (id >= B_SAY0 && id <= B_SAY_LAST)
                d_say = true;
            else
                d_header = true;
        } else {
            d_all = true;
        }
    }
    if (e.type == p4lcd::TouchEvent::UP && ev.what == pu::Pointer::TAP)
        press(ev.id);
}

/* the union of two rectangles, cut to the map's region */
static Rect map_dirty(const Rect &a, const Rect &b)
{
    int x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    int x1 = std::max(a.x + a.w, b.x + b.w), y1 = std::max(a.y + a.h, b.y + b.h);
    x0 = std::max(x0, R_MAP.x);
    y0 = std::max(y0, R_MAP.y);
    x1 = std::min(x1, R_MAP.x + R_MAP.w);
    y1 = std::min(y1, R_MAP.y + R_MAP.h);
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

static void ui_task(void *)
{
    uint32_t seen_version = 0, seen_gen = 0, seen_world = 0;
    int seen_demo = -1;
    asr::State seen_state = asr::WAITING_MODEL;
    int64_t next_stats = 0, next_meter = 0, next_fade = 0, last_fade = 0, last_t = esp_timer_get_time();
    bool seen_phrases = phrases::on();
    int last_blink = -1;
    Rect moving_drawn = {0, 0, 0, 0};
    char status_drawn[128] = "";
    for (;;) {
        /* A demo page's frames (input, the demo, what moves, at most its world: about 31 ms) run above the engine's
         * worker, 10 a second: below it, a frame that met an encoder half on core 1 waited up to 0.6 s. A whole page
         * drawn anew (up to 0.36 s) and the other pages take core 1's idle time only, as before. */
        bool demo_frame = current == DEMO;
        if (demo_frame)
            vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 1);
        const int64_t t0 = esp_timer_get_time();
        /* input: the panel's touches, then the debug link's (a demo chosen, then a page) */
        p4lcd::TouchEvent e;
        while (s_in.next(e))
            on_touch(e);
        const int dr = s_demo_req.exchange(-1);
        if (dr >= 0)
            demo::select(dr);
        const int sr = s_screen_req.exchange(-1);
        if (sr >= 0 && sr < PAGES)
            go(static_cast<Screen>(sr));
        /* the demo's phrase list while its pages are open, and its words final at a pause (the word time-out) rather
         * than when the search's hypotheses agree (both applied between chunks) */
        const int list = in_demo(current) ? 1 + demo::selected() : 0;
        if (phrases::selected() != list)
            phrases::select(list);
        asr::set_pause_commit(list ? static_cast<int>(lrintf(settings::get().word_timeout_s * 1000)) : 0);
        /* the demo: the transcript's utterances, its world */
        if (demo::tick())
            d_heard = d_actions = true;
        const float dt = static_cast<float>(t0 - last_t) / 1e6f;
        last_t = t0;
        demo::world().update(dt < 0.25f ? dt : 0.25f);
        if (demo::world().version() != seen_world || demo::selected() != seen_demo) {
            seen_world = demo::world().version();
            seen_demo = demo::selected();
            d_map = d_actions = true;
        }
        /* what changed */
        const asr::State st = asr::state();
        if (st != seen_state) {
            seen_state = st;
            if (current == DEMO) /* (only its header shows the state: the pill, the microphone's button) */
                d_header = true;
            else
                d_all = true;
        }
        if (phrases::on() != seen_phrases) { /* (also when the debug link switched them) */
            seen_phrases = phrases::on();
            d_all = true;
        }
        if (saving && asr::save_count() != s_save_seen) {
            saving = false;
            saved_gen = transcript::generation();
            d_hist = true;
        }
        if (st == asr::LOADING_MODEL && t0 > next_stats) {
            d_header = d_text = d_body = true;
            next_stats = t0 + 200000;
        }
        const uint32_t v = transcript::version(), g = transcript::generation();
        if (v != seen_version || g != seen_gen) {
            seen_version = v;
            seen_gen = g;
            d_text = d_scroll = d_hist = true;
            d_controls = true; /* Clear's state */
        }
        /* the fade's steps (every 100 ms) and the caret's blink: their bands only, unless the whole text is drawn */
        bool band_fade = false, band_caret = false;
        const int64_t fade_end = fade_until();
        if ((t0 < fade_end && t0 >= next_fade) || (t0 >= fade_end && last_fade < fade_end)) {
            band_fade = true; /* (the last step after the fade's end: the words plain) */
            next_fade = t0 + 100000;
            last_fade = t0;
        }
        const int blink = static_cast<int>(t0 / 500000) % 2;
        if (listening_now() && blink != last_blink) {
            last_blink = blink;
            band_caret = true;
        }
        if (t0 > next_stats) {
            next_stats = t0 + 500000;
            d_tiles = true;
            d_body = current != ABOUT || about_changed();
        }
        if (t0 > next_meter && st >= asr::IDLE) {
            next_meter = t0 + 100000;
            d_meter = true;
        }
        if (auto fn = s_shot_fn.exchange(nullptr)) { /* (sent by the link's tasks: never above them) */
            vTaskPrioritySet(nullptr, 6);
            fn(W, H, s_lcd.rotation(), static_cast<const uint16_t *>(s_lcd.screen()));
            if (demo_frame)
                vTaskPrioritySet(nullptr, configMAX_PRIORITIES - 1);
        }
        /* drawing (a whole page drawn anew, or another page: core 1's idle time only) */
        if (demo_frame && (d_all || current != DEMO)) {
            demo_frame = false;
            vTaskPrioritySet(nullptr, 6);
        }
        bool drew = false;
        if (d_all || (current == LIVE && d_text)) { /* the bands found anew */
            bands_reset();
            next_fade = t0 + 100000;
            last_fade = t0;
            band_fade = band_caret = false;
        }
        if (d_all) {
            draw_all();
            drew = true;
            moving_drawn = {0, 0, 0, 0};
            if (demo_page().moving)
                demo_page().moving(moving_drawn);
            world_status(status_drawn, sizeof status_drawn);
        } else {
            layout_buttons();
            if (d_header) {
                render(R_HEADER, [](Painter &p) { draw_header(p); });
                drew = true;
            }
            if (current == LIVE) {
                if (d_controls)
                    render(R_CONTROLS, [](Painter &p) { draw_controls(p); });
                else if (d_meter)
                    render(R_METER, [](Painter &p) { draw_meter(p); });
                const int64_t now = esp_timer_get_time();
                int y0, y1;
                if (d_text) {
                    render(R_TEXT, [now](Painter &p) { draw_text_area(p, now); });
                } else {
                    if (band_fade && fade_band(y0, y1))
                        render_text_band(y0, y1, now);
                    if (band_caret) {
                        caret_band(y0, y1);
                        render_text_band(y0, y1, now);
                    }
                }
                if (d_scroll)
                    render(R_SCROLL, [](Painter &p) { draw_scrollbar(p); });
                if (d_tiles)
                    render(R_TILES, [](Painter &p) { draw_tiles(p); });
                drew = drew || d_controls || d_meter || d_text || d_scroll || d_tiles || band_fade || band_caret;
            } else if (current == STATS && d_body) {
                render(R_STATS_TOP, [](Painter &p) { draw_stats_top(p); });
                render(R_STATS_GRID, [](Painter &p) { draw_stats_grid(p); });
                drew = true;
            } else if (current == HISTORY && (d_hist || d_controls)) {
                render(R_HIST_BAR, [](Painter &p) { draw_hist_bar(p); });
                render(R_HIST_LIST, [](Painter &p) { draw_hist_list(p); });
                drew = true;
            } else if (current == ABOUT && d_body) {
                render(R_BODY, [](Painter &p) { draw_about(p); });
                drew = true;
            } else if (current == RULES && d_rules) {
                render(R_BODY, [](Painter &p) { draw_rules(p); });
                drew = true;
            } else if (current == DEMO) {
                /* what moves: its box drawn again where it was and where it is (or every frame while it says so) */
                Rect now_box = {0, 0, 0, 0};
                const bool always = demo_page().moving && demo_page().moving(now_box);
                char status[128];
                world_status(status, sizeof status);
                if (d_map) {
                    render(R_MAP, [](Painter &p) { draw_world(p); });
                    drew = true;
                } else {
                    if (now_box.w && (always || now_box.x != moving_drawn.x || now_box.y != moving_drawn.y)) {
                        render(map_dirty(moving_drawn.w ? moving_drawn : now_box, now_box),
                               [](Painter &p) { draw_world(p); });
                        drew = true;
                    }
                    if (strcmp(status, status_drawn)) {
                        render(R_MAP_STATUS, [](Painter &p) { draw_world(p); });
                        drew = true;
                    }
                }
                moving_drawn = now_box;
                snprintf(status_drawn, sizeof status_drawn, "%s", status);
                if (d_heard)
                    render(R_HEARD, [](Painter &p) { draw_heard(p); });
                if (d_actions)
                    render(R_ACTIONS, [](Painter &p) { draw_actions(p); });
                if (d_say)
                    render(R_SAY, [](Painter &p) { draw_say(p); });
                drew = drew || d_heard || d_actions || d_say;
            }
        }
        d_all = d_header = d_controls = d_meter = d_text = d_scroll = d_tiles = d_body = d_hist = false;
        d_map = d_heard = d_actions = d_say = d_rules = false;
        const int64_t t1 = esp_timer_get_time();
        if (drew)
            s_frames.add(static_cast<int32_t>(t1 - t0));
        if (demo_frame) { /* back below the worker until the next frame: 100 ms after this one began, or at once
                           * when a chunk is done (its words may end an utterance: the action without the wait) */
            vTaskPrioritySet(nullptr, 6);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(std::max<int64_t>(10, 100 - (t1 - t0) / 1000)));
        } else {
            vTaskDelay(pdMS_TO_TICKS(40));
        }
    }
}

bool start()
{
    settings::load();
    const settings::Settings &se = settings::get();
    phrases::set_state(se.phrases_on, se.phrases_off); /* (list 0's: the transcriber's) */
    if (!live_init() || !stats_init() || !history_init())
        return false;
    p4lcd::Config c;
    c.frame_buffers = 1; /* the model leaves room for one */
    if (!s_lcd.open(c) || !s_ren.init(W, 64))
        return false;
    s_in.open(s_touch.open() ? &s_touch : nullptr);
    apply_settings();
    /* on core 1 below the engine's worker: the screen takes core 1's idle time only and never stops the encoder's
     * half on core 0 (on core 0 at priority 6 it took 29 % of that core while streaming: 862 vs 695 ms a chunk);
     * 16 KB: the demo's rules (about 4 KB) on top of the drawing */
    xTaskCreatePinnedToCore(ui_task, "ui", 16384, nullptr, 6, &s_ui_task, 1);
    asr::set_chunk_listener([] {
        if (s_ui_task)
            xTaskNotifyGive(s_ui_task);
    });
    return true;
}

void tap(int x, int y) { s_in.tap(x, y); }

void drag(int x0, int y0, int x1, int y1) { s_in.drag(x0, y0, x1, y1); }

bool show(const char *id)
{
    for (int i = 0; i < demo::COUNT; i++) /* a demo's id: that demo's page (chosen on the UI task) */
        if (!strcmp(id, demo_page(i).id)) {
            s_demo_req = i;
            s_screen_req = DEMO;
            return true;
        }
    for (int i = 0; i < PAGES; i++)
        if (!strcmp(id, PAGE_IDS[i])) {
            s_screen_req = i;
            return true;
        }
    return false;
}

void shot(void (*fn)(int w, int h, int rotation, const uint16_t *px)) { s_shot_fn = fn; }

Check check(bool reset)
{
    const pu::FitStats fs = pu::fitStats();
    const Check c = {fs.cut, fs.shrunk, s_frames.frames, static_cast<float>(s_frames.meanMs()),
                     static_cast<float>(s_frames.max_us / 1000.0)};
    if (reset)
        s_frames.reset();
    return c;
}

} // namespace ui
