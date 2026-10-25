// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Home (two tiles: the transcriber, voice control) and the demos' page (a card per demo). The tiles and cards are
 * buttons laid out in ui.cpp; drawn here, with an accent border while pressed.
 */
#include <cstdio>
#include "demo.hpp"
#include "ui_internal.hpp"

namespace ui {

void draw_home_header(Painter &p)
{
    p.fill(theme->bg);
    const Rect r = p.fit({24, 10, 260, 48}, "esp32-pie-asr", style(28, theme->ink, true, Align::LEFT, VAlign::MIDDLE),
                         "home.title");
    char s[48];
    Color bg, fg;
    state_pill(s, sizeof s, bg, fg);
    pu::pill(p, r.x + r.w + 16, 20, 30, s, bg, fg, 16);
    p.fit({600, 10, W - 24 - 600, 48}, "Speech recognition on the ESP32-P4",
          style(16, theme->muted, false, Align::RIGHT, VAlign::MIDDLE), "home.note");
    p.rect(0, 67, W, 1, theme->line);
}

static void tags(Painter &p, int cx, int y, const char *const *t, int n)
{
    int w[4], total = 0;
    for (int i = 0; i < n; i++) {
        w[i] = p4lcd::Canvas::textBox(0, 0, t[i], style(14, theme->muted, true)).w + 24;
        total += w[i] + (i ? 8 : 0);
    }
    int x = cx - total / 2;
    for (int i = 0; i < n; i++) {
        pu::pill(p, x, y, 28, t[i], theme->soft, theme->muted, 14);
        x += w[i] + 8;
    }
}

void draw_home(Painter &p)
{
    p.fill(theme->bg);
    p.fit({24, 92, W - 48, 40}, "What do you want to try?",
          style(26, theme->ink, true, Align::CENTER, VAlign::MIDDLE), "home.ask");
    p.fit({24, 128, W - 48, 30}, "Everything below runs on this chip: no network, no cloud.",
          style(17, theme->muted, false, Align::CENTER, VAlign::MIDDLE), "home.sub");
    struct Tile {
        int id;
        const char *name, *line;
        const char *tags[3];
        void (*icon)(Painter &, int, int, Color, Color);
    };
    const Tile tiles[2] = {
        {B_TILE_LIVE, "Live transcription", "Speak, and every word appears on the screen",
         {"open vocabulary", "beam search", "phrases"}, icon_transcribe},
        {B_TILE_DEMOS, "Voice control", "Say a command, see the action and what triggered it",
         {"six demos", "no training", "word rules"}, icon_control},
    };
    for (const Tile &t : tiles) {
        const Rect r = buttons[t.id].r;
        const bool down = buttons[t.id].pressed;
        p.roundFrame(r.x, r.y, r.w, r.h, 18, down ? theme->accent : theme->line, theme->panel, down ? 2 : 1);
        const int cx = r.x + r.w / 2;
        const Color tint = pu::mix(theme->panel, theme->accent, 0.16f);
        p.round(cx - 80, r.y + 34, 160, 128, 24, tint);
        t.icon(p, cx, r.y + 98, theme->accent, tint);
        p.fit({r.x + 16, r.y + 178, r.w - 32, 40}, t.name, style(27, theme->ink, true, Align::CENTER, VAlign::MIDDLE),
              "home.tile");
        p.fit({r.x + 16, r.y + 218, r.w - 32, 28}, t.line,
              style(16, theme->muted, false, Align::CENTER, VAlign::MIDDLE), "home.line");
        tags(p, cx, r.y + 266, t.tags, 3);
    }
    p.fit({24, 532, W - 48, 30},
          "4.34 % WER on LibriSpeech, 0.70x real time, 26.65 MB of 2/4-bit weights, github.com/BoumedineBillal/esp32-pie-asr",
          style(14, theme->faint, false, Align::CENTER, VAlign::MIDDLE), "home.foot");
}

void draw_demos(Painter &p)
{
    p.fill(theme->bg);
    TextStyle st = style(16, theme->muted);
    st.wrap = W - 48;
    p.fit({24, 84, W - 48, 48},
          "Each demo listens for its own words and turns them into actions. The words are rules, not a language "
          "model: you see exactly which words fired which action.",
          st, "demos.intro");
    /* a card per demo */
    const Color tint = pu::mix(theme->panel, theme->accent, 0.16f);
    for (int i = 0; i < demo::COUNT; i++) {
        const DemoPage &d = demo_page(i);
        const Rect r = buttons[B_CARD0 + i].r;
        const bool down = buttons[B_CARD0 + i].pressed;
        p.roundFrame(r.x, r.y, r.w, r.h, 14, down ? theme->accent : theme->line, theme->panel, down ? 2 : 1);
        p.round(r.x + 16, r.y + 16, 52, 52, 13, tint);
        d.icon(p, r.x + 42, r.y + 42, theme->accent, tint);
        p.fit({r.x + 80, r.y + 16, r.w - 96, 30}, d.title, style(21, theme->ink, true), "demos.name");
        p.fit({r.x + 80, r.y + 46, r.w - 96, 22}, d.sub, style(14, theme->muted), "demos.sub");
        p.fit({r.x + 16, r.y + 84, 200, 18}, "say", style(12, theme->faint, true), "demos.say");
        char ex[96];
        snprintf(ex, sizeof ex, "\"%s\"", d.example);
        TextStyle es = style(15, theme->ink);
        es.wrap = r.w - 32;
        p.fit({r.x + 16, r.y + 102, r.w - 32, 46}, ex, es, "demos.example");
        p.rect(r.x + 16, r.y + r.h - 46, r.w - 32, 1, theme->line);
        char n[32];
        snprintf(n, sizeof n, "%d actions", demos::get(i).n);
        p.fit({r.x + 16, r.y + r.h - 40, 160, 32}, n, style(13, theme->muted, false, Align::LEFT, VAlign::MIDDLE),
              "demos.count");
        p.fit({r.x + r.w - 120, r.y + r.h - 40, 104, 32}, "Open  >",
              style(15, theme->accent, true, Align::RIGHT, VAlign::MIDDLE), "demos.open");
    }
}

} // namespace ui
