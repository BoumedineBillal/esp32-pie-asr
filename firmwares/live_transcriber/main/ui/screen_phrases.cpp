// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Phrases: the phrase file's words and sentences (phrases.hpp), each a switch, and the switch for all of them; their
 * buttons are laid out with the others in ui.cpp, their effect is ui.cpp's press().
 */
#include "app.hpp"
#include "ui_internal.hpp"

namespace ui {

static void column(Painter &p, const Rect &r, const char *name, int n, bool word, const char *id)
{
    int on = 0;
    for (int j = 0; j < n; j++)
        on += phrases::enabled(word ? phrases::word(j) : phrases::sentence(j));
    char title[48];
    snprintf(title, sizeof title, "%s (%d of %d on)", name, on, n);
    pu::card(p, *theme, r, title, id);
    if (!n)
        p.fit({r.x + 16, r.y + 52, r.w - 32, 30}, "none in the file", style(18, theme->muted), id);
}

void draw_phrases(Painter &p)
{
    p.fill(theme->bg);
    p.fit({20, 74, 520, 34}, "Phrases", style(26, theme->ink, true, Align::LEFT, VAlign::MIDDLE), "ph.title");
    char note[200];
    const int ign = phrases::ignored();
    if (ign)
        snprintf(note, sizeof note, "The lines that are on are listened for harder. %d line%s of %s ignored (at "
                 "most 10 words and 10 sentences).", ign, ign == 1 ? "" : "s", app::PHRASES_FILE);
    else
        snprintf(note, sizeof note, "The lines that are on are listened for harder. The list: %s, from %s.",
                 app::PHRASES_FILE, phrases::source());
    p.fit({20, 108, 760, 26}, note, style(17, theme->muted, false, Align::LEFT, VAlign::MIDDLE), "ph.note");
    p.fit({720, 82, 150, 44}, "All phrases", style(21, theme->ink, true, Align::RIGHT, VAlign::MIDDLE), "ph.all");
    column(p, PH_WORDS, "Words", phrases::words(), true, "ph.words");
    column(p, PH_SENTS, "Sentences", phrases::sentences(), false, "ph.sents");
    pu::button(p, *theme, buttons[B_PH_ALL], "btn.ph.all", 20);
    for (int j = 0; j < phrases::words(); j++)
        pu::button(p, *theme, buttons[B_PH_WORD0 + j], "btn.ph.word", 19);
    for (int j = 0; j < phrases::sentences(); j++)
        pu::button(p, *theme, buttons[B_PH_SENT0 + j], "btn.ph.sent", 19);
}

} // namespace ui
