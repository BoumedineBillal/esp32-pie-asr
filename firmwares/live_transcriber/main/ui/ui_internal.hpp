// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * What the screens share (ui.cpp: the frame loop, the buttons, the touch; screen_*.cpp: each screen's drawing).
 *
 * Layout (1024 x 600, upright for the user: p4lcd's rotation 180):
 *   header    0..68    title, status pill (", phrases on" while they are), six tabs
 *   Live      controls 68..132 (Start, Stop, Clear, level), transcript panel 132..472, six tiles 472..600
 *   Stats     the compute chart and the word delays 68..340, twelve numbers 340..600
 *   History   a bar 68..124 (count, the last save, Save to SD card), the lines 124..600
 *   Phrases   a bar 68..140 (the switch for all), Words and Sentences 148..592, a switch per line
 *   Settings  eight rows 68..600
 *   About     68..600
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
enum Screen { LIVE, STATS, HISTORY, PHRASES, SETTINGS, ABOUT, SCREENS };

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

/* the buttons of every screen (ui.cpp lays them out for the state of the moment) */
enum ButtonId {
    B_TAB0, B_TAB_LAST = B_TAB0 + SCREENS - 1, B_START, B_STOP, B_CLEAR, B_BACK_LIVE, B_SAVE,
    B_GAIN_M, B_GAIN_P, B_WT_M, B_WT_P, B_PAUSE_M, B_PAUSE_P, B_SIZE_S, B_SIZE_M, B_SIZE_L, B_DARK, B_LIGHT,
    B_TENT, B_TS, B_FOLLOW,
    B_PH_ALL, B_PH_WORD0, B_PH_SENT0 = B_PH_WORD0 + phrases::COLUMN, B_PH_LAST = B_PH_SENT0 + phrases::COLUMN - 1,
    B_COUNT
};

/* Phrases' columns: the cards, and row j's top */
constexpr Rect PH_WORDS = {16, 148, 320, 444};
constexpr Rect PH_SENTS = {352, 148, 656, 444};
inline int ph_row_y(int j) { return 196 + j * 40; }
extern Button buttons[B_COUNT];

extern const pu::Theme *theme;  /* dark or light (Settings) */
extern Screen current;          /* the screen shown */
extern pu::Scroll live_view;    /* the transcript (scrolled back, a band at its bottom holds "Back to live") */
extern pu::Scroll hist_view;    /* History's lines */
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

} // namespace ui
