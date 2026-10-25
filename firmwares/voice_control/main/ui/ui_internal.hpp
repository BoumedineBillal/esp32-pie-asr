// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * What the screens share (ui.cpp: the frame loop, the buttons, the touch; screen_*.cpp: each screen's drawing).
 *
 * Pages: Home (two tiles), the transcriber (six tabs: Live, Stats, History, Phrases, Settings, About), Voice control
 * (six demo cards), a demo's page and its rules (the selected demo's). Dark theme only.
 *
 * Layout (1024 x 600, upright for the user: p4lcd's rotation 180):
 *   header    0..68    the home button (not on Home), the title, the status pill, the page's buttons or tabs
 *   Live      controls 68..132 (Start, Stop, Clear, level), transcript panel 132..472, six tiles 472..600
 *   Stats     the compute chart and the word delays 68..340, twelve numbers 340..600
 *   History   a bar 68..124 (count, the last save, Save to SD card), the lines 124..600
 *   Phrases   a bar 68..140 (the switch for all), Words and Sentences 148..592, a switch per line
 *   Settings  seven rows 68..600
 *   About     68..600
 *   Demos     an intro 84..132, six cards in two rows 144..584
 *   a demo    its world 16..604 x 80..584 (the status line at its bottom); Heard, Actions, Say in a column 616..1008
 *   Rules     an intro 78..122, the rows 130..584 (they scroll)
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include "asr_service.hpp"
#include "p4lcd.hpp"
#include "p4lcd_ui.hpp"
#include "phrases.hpp"

namespace ui {

using p4lcd::Align;
using p4lcd::Color;
using p4lcd::Rect;
using p4lcd::TextStyle;
using p4lcd::VAlign;
using p4lcd::ui::Button;
using p4lcd::ui::Kind;
using p4lcd::ui::Painter;
using p4lcd::ui::style;
namespace pu = p4lcd::ui;

constexpr int W = 1024, H = 600;
/* the transcriber's six tabs first (SCREENS of them), then the other pages (DEMO, RULES: the selected demo's) */
enum Screen { LIVE, STATS, HISTORY, PHRASES, SETTINGS, ABOUT, SCREENS, HOME = SCREENS, DEMOS, DEMO, RULES, PAGES };
inline bool transcriber(int s) { return s < SCREENS; }
inline bool in_demo(int s) { return s == DEMO || s == RULES; }

/* the regions, each drawn by its own function */
constexpr Rect R_HEADER = {0, 0, W, 68};
constexpr Rect R_CONTROLS = {0, 68, W, 64};
constexpr Rect R_METER = {430, 88, 578, 24};
constexpr Rect R_PANEL = {0, 132, W, 340};
constexpr Rect PANEL = {16, 136, 992, 332};
constexpr Rect R_TEXT = {34, 150, 948, 304};
constexpr Rect R_SCROLL = {988, 150, 10, 304};
constexpr Rect R_TILES = {0, 472, W, 128};
constexpr Rect R_STATS_TOP = {0, 68, W, 272};
constexpr Rect R_STATS_GRID = {0, 340, W, 260};
constexpr Rect R_HIST_BAR = {0, 68, W, 56};
constexpr Rect R_HIST_LIST = {0, 124, W, 476};
constexpr Rect HIST_CARD = {16, 124, 992, 468};
constexpr Rect R_HIST_TEXT = {32, 136, 948, 444};
constexpr Rect R_HIST_SCROLL = {984, 136, 10, 444};
constexpr Rect R_BODY = {0, 68, W, 532};
/* a demo: its world's panel (MAP; the vacuum's map units = pixels from its top-left), the status line at its bottom,
 * the right column's panels */
constexpr Rect MAP = {16, 80, 588, 504};
constexpr Rect R_MAP = {0, 68, 610, 532};
constexpr Rect R_MAP_STATUS = {24, 560, 572, 22};
constexpr Rect HEARD = {616, 80, 392, 120};
constexpr Rect ACTIONS = {616, 208, 392, 216};
constexpr Rect SAY = {616, 432, 392, 152};
constexpr Rect R_HEARD = {610, 68, 414, 136};
constexpr Rect R_ACTIONS = {610, 204, 414, 224};
constexpr Rect R_SAY = {610, 428, 414, 172};

/* the buttons of every page (ui.cpp lays them out for the state of the moment) */
constexpr int SAY_CHIPS = 6;
enum ButtonId {
    B_TAB0, B_TAB_LAST = B_TAB0 + SCREENS - 1, B_START, B_STOP, B_CLEAR, B_BACK_LIVE, B_SAVE,
    B_GAIN_M, B_GAIN_P, B_WT_M, B_WT_P, B_PAUSE_M, B_PAUSE_P, B_SIZE_S, B_SIZE_M, B_SIZE_L,
    B_TENT, B_TS, B_FOLLOW,
    B_PH_ALL, B_PH_WORD0, B_PH_SENT0 = B_PH_WORD0 + phrases::COLUMN, B_PH_LAST = B_PH_SENT0 + phrases::COLUMN - 1,
    B_HOME, B_TILE_LIVE, B_TILE_DEMOS, B_CARD0, B_CARD_LAST = B_CARD0 + 5, B_DEMO_RULES, B_DEMO_RESET, B_DEMO_MIC,
    B_RULES_BACK, B_SAY0, B_SAY_LAST = B_SAY0 + SAY_CHIPS - 1,
    B_COUNT
};

/* the demos' page: card i */
inline Rect card_rect(int i) { return {16 + (i % 3) * 337, 144 + (i / 3) * 226, 318, 214}; }
/* the rules page: the rows' box (they scroll inside it) */
constexpr Rect R_RULES_ROWS = {16, 130, 992, 454};

/* Phrases' columns: the cards, and row j's top */
constexpr Rect PH_WORDS = {16, 148, 320, 444};
constexpr Rect PH_SENTS = {352, 148, 656, 444};
inline int ph_row_y(int j) { return 196 + j * 40; }
extern Button buttons[B_COUNT];

extern const pu::Theme *theme;  /* dark */
extern Screen current;          /* the page shown */
extern pu::Scroll live_view;    /* the transcript (scrolled back, a band at its bottom holds "Back to live") */
extern pu::Scroll hist_view;    /* History's lines */
extern pu::Scroll rules_view;   /* the rules page's rows */
extern bool saving;             /* Save to SD card pressed, not done yet */
extern uint32_t saved_gen;      /* the transcript's generation when the last save was done */

p4lcd::Display &display();
pu::Renderer &renderer();
template <typename F> void render(const Rect &r, F &&draw) { renderer().render(display(), r, draw); }

/* Settings' rows: the controls end at ROW_RIGHT; row i's top */
constexpr int ROW_RIGHT = 1008 - 20;
inline int row_y(int i) { return 76 + i * 65; }

inline void fmt_time(char *s, size_t n, int32_t ms)
{
    const int sec = ms / 1000;
    snprintf(s, n, "%d:%02d", sec / 60, sec % 60);
}

inline bool listening_now()
{
    const asr::State st = asr::state();
    return st == asr::LISTENING || st == asr::FINISHING;
}

/* the status pill's text and colours for the recogniser's state */
void state_pill(char *s, size_t n, Color &bg, Color &fg);

/* ---- screen_live.cpp ---- */
bool live_init();
void draw_controls(Painter &p);
void draw_meter(Painter &p);
void draw_text_area(Painter &p, int64_t now);
void render_text_band(int y0, int y1, int64_t now);   /* rows [y0, y1) of the text area again */
void draw_scrollbar(Painter &p);
void draw_tiles(Painter &p);
/* the text area's bands as its last drawing found them (screen rows): the words still fading in, the caret's line;
 * between transcript changes only these are drawn again */
int64_t fade_until();
void bands_reset();
bool fade_band(int &y0, int &y1);
void caret_band(int &y0, int &y1);

/* ---- screen_stats.cpp, screen_history.cpp, screen_settings.cpp, screen_about.cpp ---- */
bool stats_init();
void draw_stats_top(Painter &p);
void draw_stats_grid(Painter &p);
bool history_init();
void draw_hist_bar(Painter &p);
void draw_hist_list(Painter &p);
void draw_settings(Painter &p);
void draw_phrases(Painter &p);
void draw_about(Painter &p);
bool about_changed();   /* About's changing line (memory, this session's compute) is not what was drawn */

/* ---- screen_home.cpp: Home and the demos' page ---- */
void draw_home_header(Painter &p);
void draw_home(Painter &p);
void draw_demos(Painter &p);

/* ---- a demo's page (demo_pages.cpp: one per demo, in demo::COUNT's order) ---- */
struct DemoPage {
    const char *id;                     /* the debug link's page name: "vacuum" */
    const char *title, *sub;            /* "Robot vacuum", "rooms, messes, the dock, the suction" */
    const char *example;                /* the demos' page: "clean the kitchen, not the bedroom" */
    const char *const *say;             /* SAY_CHIPS examples */
    const char *no_words;               /* reason texts: none of its words; a verb without what it acts on */
    const char *no_target;
    void (*icon)(Painter &p, int cx, int cy, Color c, Color bg);
    void (*draw)(Painter &p);           /* its world in MAP (the status line included) */
    /* what moves every frame (a robot, a belt): its screen box; true when it must be drawn again even where it is */
    bool (*moving)(Rect &box);
};
const DemoPage &demo_page(int i);
const DemoPage &demo_page();            /* the selected demo's */

/* ---- screen_demo.cpp: what every demo's page has ---- */
void draw_world(Painter &p);            /* the selected demo's world (its draw) */
void world_status(char *s, size_t n);   /* the status line at the panel's bottom */
void draw_world_status(Painter &p);
void draw_heard(Painter &p);
void draw_actions(Painter &p);
void draw_say(Painter &p);
void draw_rules(Painter &p);
/* the panel's frame and a world's common marks (for the env_*.cpp) */
void world_panel(Painter &p);
Color floor_color();

/* ---- env_*.cpp: each demo's world, drawn ---- */
void draw_vacuum(Painter &p);
bool vacuum_moving(Rect &box);
void draw_smarthome(Painter &p);
void draw_car(Painter &p);
void draw_factory(Painter &p);
bool factory_moving(Rect &box);
void draw_kitchen(Painter &p);
void draw_presenter(Painter &p);
bool presenter_moving(Rect &box);

/* ---- icons.cpp: drawn from rounded rectangles, discs and lines (c: the ink) ---- */
void icon_home(Painter &p, int cx, int cy, Color c, Color bg);
void icon_mic(Painter &p, int cx, int cy, Color c, Color bg, float k = 1.0f);
void icon_transcribe(Painter &p, int cx, int cy, Color c, Color bg);
void icon_control(Painter &p, int cx, int cy, Color c, Color bg);
void icon_vacuum(Painter &p, int cx, int cy, Color c, Color bg);
void icon_house(Painter &p, int cx, int cy, Color c, Color bg);
void icon_car(Painter &p, int cx, int cy, Color c, Color bg);
void icon_factory(Painter &p, int cx, int cy, Color c, Color bg);
void icon_kitchen(Painter &p, int cx, int cy, Color c, Color bg);
void icon_presenter(Painter &p, int cx, int cy, Color c, Color bg);
void arc(Painter &p, float cx, float cy, float rx, float ry, float a0, float a1, Color c, int t);

} // namespace ui
