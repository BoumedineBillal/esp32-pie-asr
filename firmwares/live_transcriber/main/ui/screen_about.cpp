// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * About: the project, its author and link, what each part does, and a line with the memory and this session's
 * compute (About is drawn again only when that line changes).
 */
#include <cstring>
#include "ui_internal.hpp"

namespace ui {

static char s_now[200];

bool about_changed()
{
    const asr::Stats s = asr::stats();
    char now[sizeof s_now];
    const int o = snprintf(now, sizeof now, "PSRAM %u KB free (lowest %u KB), internal RAM %u KB free.",
                           (unsigned)(s.psram_free / 1024), (unsigned)(s.psram_min / 1024),
                           (unsigned)(s.internal_free / 1024));
    if (s.chunks)
        snprintf(now + o, sizeof now - o, " This session: %d ms of compute per 1.28 s chunk (mean of %d).",
                 (int)s.mean_ms, s.chunks);
    if (!strcmp(now, s_now))
        return false;
    strcpy(s_now, now);
    return true;
}

void draw_about(Painter &p)
{
    p.fill(theme->bg);
    const Rect c = {16, 76, 992, 516};
    pu::card(p, *theme, c, "esp32-pie-asr, by Boumedine Billal", "about.title");
    if (!s_now[0])
        about_changed();
    p.fit({c.x + 24, c.y + 48, c.w - 48, 30}, "github.com/BoumedineBillal/esp32-pie-asr",
          style(21, theme->accent, true, Align::LEFT, VAlign::MIDDLE), "about.link");
    struct Row {
        const char *label, *text;
    };
    const Row rows[] = {
        {"Model", "Kroko by Banafo, 65.7M parameters, its weights brought from 71 MB to 26.65 MB in 2 and 4 bits "
                  "without retraining. LibriSpeech test-clean: 4.34 % of the words wrong."},
        {"Engine", "1.28 s of audio computed in 0.90 s on both cores, with integer arithmetic and hand-written PIE "
                   "SIMD kernels. The chip's output is the same, bit for bit, as the engine's on a PC."},
        {"Streaming", "One encoder step per 1.28 s with caches of fixed size: the memory stays the same however long "
                      "you speak. A word is final about 2 s after you say it."},
        {"Search", "A beam search of 4 hypotheses picks the words. The Phrases tab makes it listen harder for the "
                   "words and sentences you choose."},
        {"App", "The microphone, the screen and touch, the microSD card, the transcript with its history and "
                "statistics: all on this chip."},
        {"Now", s_now},
    };
    constexpr int LABEL_W = 124;
    int y = c.y + 92;
    for (const Row &r : rows) {
        TextStyle st = style(19, theme->ink);
        st.wrap = c.w - 48 - LABEL_W;
        const Rect box = p4lcd::Canvas::textBox(0, 0, r.text, st);
        p.fit({c.x + 24, y, LABEL_W - 12, 26}, r.label, style(19, theme->muted, true), "about.label");
        p.fit({c.x + 24 + LABEL_W, y, c.w - 48 - LABEL_W, box.h + 4}, r.text, st, "about.line");
        y += box.h + 14;
    }
}

} // namespace ui
