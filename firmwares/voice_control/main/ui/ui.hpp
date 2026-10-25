// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The screen: p4lcd's Display with one frame buffer (the model leaves no room for two), the touch panel, and the UI
 * task that draws the screens (Home, the transcriber's Live, Stats, History, Phrases, Settings and About, the demos'
 * cards, a demo's page and its rules) with p4lcd's UI layer (p4lcd_ui.hpp).
 */
#pragma once

#include <cstdint>

namespace ui {

/* the settings, the Display (one frame buffer), the touch panel, the UI task; false if the Display did not open */
bool start();

/* ---- for the debug link (debug/commands.cpp) ---- */
void tap(int x, int y);                     /* a touch at (x, y), down then up */
void drag(int x0, int y0, int x1, int y1);  /* a finger moved from (x0, y0) to (x1, y1) */
/* home, live, stats, history, phrases, settings, about, demos, demo, rules (the selected demo's), or a demo's id
 * (vacuum, smarthome, car, factory, kitchen, presenter: that demo's page) */
bool show(const char *id);
/* the screen's pixels (the panel's RGB565, before its rotation) given to fn by the UI task between two frames */
void shot(void (*fn)(int w, int h, int rotation, const uint16_t *px));
struct Check {
    int overflows, shrunk;                  /* p4lcd's text-fit check */
    int frames;
    float mean_ms, max_ms;                  /* the frames' drawing times */
};
Check check(bool reset);                    /* reset: the frames counted anew */

} // namespace ui
