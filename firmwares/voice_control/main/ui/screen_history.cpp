// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * History: every line of the transcript with its start time, in a scroll view (it follows the newest line; a drag
 * reads back); the count of lines and words, the last save's file, and "Save to SD card".
 */
#include <algorithm>
#include "esp_heap_caps.h"
#include "transcript.hpp"
#include "ui_internal.hpp"

namespace ui {

static constexpr int MAX_LINES = 1500;
static int *s_heights;   /* each line's height, PSRAM */
static uint32_t s_version = 0xffffffff, s_gen = 0xffffffff;
static int s_total;

bool history_init()
{
    s_heights = static_cast<int *>(heap_caps_calloc(MAX_LINES, sizeof(int), MALLOC_CAP_SPIRAM));
    return s_heights != nullptr;
}

/* line l's words joined into buf */
static void line_text(int l, char *buf, size_t n)
{
    const int nw = transcript::words();
    const uint32_t a = transcript::line(l).first_word,
                   e = l + 1 < transcript::lines() ? transcript::line(l + 1).first_word : nw;
    size_t o = 0;
    for (uint32_t i = a; i < e && o + 2 < n; i++) {
        const transcript::Word &w = transcript::word(i);
        o += snprintf(buf + o, n - o, "%s%s", w.space && i > a ? " " : "", transcript::text(w));
    }
    buf[o < n ? o : n - 1] = 0;
}

static TextStyle line_style()
{
    TextStyle st = style(21, theme->ink);
    st.wrap = R_HIST_TEXT.w - 110;
    return st;
}

/* the lines' heights (their joined words, wrapped): measured again only when the transcript changed */
static int layout()
{
    if (transcript::version() == s_version && transcript::generation() == s_gen)
        return s_total;
    transcript::lock();
    s_version = transcript::version();
    s_gen = transcript::generation();
    const int nl = std::min(transcript::lines(), MAX_LINES);
    const TextStyle st = line_style();
    static char buf[2048];
    int total = 0;
    for (int l = 0; l < nl; l++) {
        line_text(l, buf, sizeof buf);
        const int h = buf[0] ? p4lcd::Canvas::textBox(0, 0, buf, st).h + 18 : 0;
        s_heights[l] = h;
        total += h;
    }
    transcript::unlock();
    s_total = total;
    return total;
}

void draw_hist_bar(Painter &p)
{
    p.fill(theme->bg);
    char s[192];
    transcript::lock();
    int o = snprintf(s, sizeof s, "%d lines, %d words", transcript::words() ? transcript::lines() : 0,
                     transcript::words());
    const bool same = transcript::generation() == saved_gen; /* the last save is of this transcript */
    transcript::unlock();
    bool ok = false;
    const char *r = asr::save_result(&ok);
    if (saving)
        snprintf(s + o, sizeof s - o, "; saving...");
    else if (same && asr::save_count())
        snprintf(s + o, sizeof s - o, ok ? "; saved as %s" : "; not saved: %s", r);
    p.fit({20, 74, 770, 44}, s, style(21, theme->muted, false, Align::LEFT, VAlign::MIDDLE), "hist.summary");
    pu::button(p, *theme, buttons[B_SAVE], "btn.save");
}

void draw_hist_list(Painter &p)
{
    p.fill(theme->bg);
    pu::card(p, *theme, HIST_CARD);
    hist_view.update(layout());
    Painter c = p.clip(R_HIST_TEXT); /* the rows, cut at the text box */
    transcript::lock();
    const int nl = std::min(transcript::lines(), MAX_LINES), nw = transcript::words();
    const TextStyle st = line_style(), tm = style(17, theme->muted);
    int y = R_HIST_TEXT.y - hist_view.offset;
    const Rect band = p.area();
    static char buf[2048];
    for (int l = 0; l < nl; y += s_heights[l], l++) {
        if (!s_heights[l] || y + s_heights[l] < band.y || y > band.y + band.h)
            continue;
        if (y < R_HIST_TEXT.y - s_heights[l] || y > R_HIST_TEXT.y + R_HIST_TEXT.h)
            continue;
        line_text(l, buf, sizeof buf);
        char ts[16];
        fmt_time(ts, sizeof ts, transcript::line(l).start_ms);
        c.text(R_HIST_TEXT.x, y + 4, ts, tm);
        c.text(R_HIST_TEXT.x + 90, y, buf, st);
        if (l + 1 < nl)
            c.rect(R_HIST_TEXT.x, y + s_heights[l] - 9, R_HIST_TEXT.w - 20, 1, theme->line);
    }
    if (!nw)
        c.fit({R_HIST_TEXT.x, R_HIST_TEXT.y, R_HIST_TEXT.w, 40}, "Nothing yet: the lines you say show here.",
              style(21, theme->muted), "hist.empty");
    transcript::unlock();
    /* the edges where more lies beyond (as on Live), then its scrollbar */
    hist_view.edges(c, theme->panel);
    hist_view.scrollbar(p, R_HIST_SCROLL.x, theme->mark);
}

} // namespace ui
