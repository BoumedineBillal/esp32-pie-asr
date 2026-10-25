// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Settings: seven rows (the microphone's gain, the word rules, the text size, the grey word, timestamps, following the
 * newest words; this firmware is dark only); their buttons are laid out with the others in ui.cpp, their effect is
 * ui.cpp's press().
 */
#include "settings.hpp"
#include "ui_internal.hpp"

namespace ui {

static void row(Painter &p, int i, const char *label, const char *note, const char *id)
{
    pu::settingRow(p, *theme, {16, row_y(i), 992, 58}, label, note, id);
}

static void value(Painter &p, int i, const char *v, const char *id)
{
    pu::stepperValue(p, *theme, pu::Stepper::at(ROW_RIGHT, row_y(i)).value, v, id);
}

void draw_settings(Painter &p)
{
    p.fill(theme->bg);
    const settings::Settings &se = settings::get();
    char v[32];
    row(p, 0, "Microphone gain", "54 dB: the fewest errors measured, with room for a loud voice", "set.gain");
    snprintf(v, sizeof v, "%.0f dB", se.gain_db);
    value(p, 0, v, "set.gain.v");
    row(p, 1, "A word is final after", "this long without a new token (default 0.5 s)", "set.wt");
    snprintf(v, sizeof v, "%.1f s", se.word_timeout_s);
    value(p, 1, v, "set.wt.v");
    row(p, 2, "A new line after a pause of", "default 1.5 s", "set.pause");
    snprintf(v, sizeof v, "%.1f s", se.pause_s);
    value(p, 2, v, "set.pause.v");
    row(p, 3, "Text size", "the transcript on Live", "set.size");
    row(p, 4, "Show the word being written", "in grey, until it is final", "set.tent");
    row(p, 5, "Timestamps", "each line's start time on Live", "set.ts");
    row(p, 6, "Follow the newest words", "the transcript scrolls by itself", "set.follow");
    for (int id = B_GAIN_M; id <= B_FOLLOW; id++)
        pu::button(p, *theme, buttons[id], "btn.setting", 20);
}

} // namespace ui
