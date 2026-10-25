// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Stats: the compute of the last 60 chunks (features, encoder, beam search, stacked, against real time), the word
 * delays' histogram, and twelve numbers.
 */
#include <algorithm>
#include "esp_heap_caps.h"
#include "transcript.hpp"
#include "ui_internal.hpp"

namespace ui {

static constexpr int MAX_DELAYS = 2000; /* the histogram's values */
static int32_t *s_dv;                   /* PSRAM */

bool stats_init()
{
    s_dv = static_cast<int32_t *>(heap_caps_calloc(MAX_DELAYS, sizeof(int32_t), MALLOC_CAP_SPIRAM));
    return s_dv != nullptr;
}

void draw_stats_top(Painter &p)
{
    p.fill(theme->bg);
    /* the compute per chunk: the last 60, stacked (features, encoder, search), against real time (1.28 s) */
    const Rect c1 = {16, 72, 664, 264};
    pu::card(p, *theme, c1, "Compute per 1.28 s chunk (the last 60)", "stats.chart.title");
    static asr::Chunk ch[60];
    static uint16_t part[3][60];
    const int n = asr::recent_chunks(ch, 60);
    int top_ms = 1600;
    for (int i = 0; i < n; i++) {
        top_ms = std::max(top_ms, static_cast<int>(ch[i].total_ms) + 100);
        part[0][i] = ch[i].fbank_ms;
        part[1][i] = ch[i].encode_ms;
        part[2][i] = ch[i].search_ms;
    }
    const pu::Series ser[3] = {{part[0], theme->ok, "features"}, {part[1], theme->accent, "encoder"},
                               {part[2], theme->warn, "beam search"}};
    pu::BarChart bc;
    bc.plot = {c1.x + 62, c1.y + 56, c1.w - 80, c1.h - 96};
    bc.slots = 60;
    bc.count = n;
    bc.top = (top_ms + 399) / 400 * 400;
    bc.grid = 400;
    bc.series = ser;
    bc.nseries = 3;
    bc.ref = 1280;
    bc.ref_label = "real time: 1280 ms";
    pu::barChart(p, *theme, bc, "stats.chart");
    pu::legend(p, *theme, c1.x + 62, c1.y + c1.h - 30, ser, 3, "stats.legend");
    if (!n)
        p.fit({bc.plot.x, bc.plot.y + bc.plot.h / 2 - 15, bc.plot.w, 30}, "No chunk yet: press Start on Live.",
              style(18, theme->muted, false, Align::CENTER), "stats.empty");

    /* the word delays: spoken until -> final, in 0.5 s bins */
    const Rect c2 = {690, 72, 318, 264};
    pu::card(p, *theme, c2, "Word delay (s)", "stats.hist.title");
    transcript::lock();
    const int nd = transcript::delay_values(s_dv, MAX_DELAYS);
    transcript::unlock();
    static int bins[9];
    std::fill(bins, bins + 9, 0);
    for (int i = 0; i < nd; i++)
        bins[std::min(8, std::max(0, static_cast<int>(s_dv[i] / 500)))]++;
    static const char *const labels[5] = {"0", "1", "2", "3", "4+"};
    pu::Histogram hg;
    hg.plot = {c2.x + 18, c2.y + 56, c2.w - 36, c2.h - 100};
    hg.bins = bins;
    hg.nbins = 9;
    hg.labels = labels;
    hg.nlabels = 5;
    hg.label_every = 2;
    pu::histogram(p, *theme, hg, "stats.hist.axis");
    char s[64];
    if (nd)
        snprintf(s, sizeof s, "%d words", nd);
    else
        snprintf(s, sizeof s, "no words yet");
    /* in the title's row (under the plot it met the axis's "4+") */
    p.fit({c2.x + c2.w / 2, c2.y + 16, c2.w / 2 - 16, 22}, s, style(15, theme->muted, false, Align::RIGHT),
          "stats.hist.n");
}

static void number_card(Painter &p, int i, const char *label, const char *value, const char *id)
{
    pu::numberCard(p, *theme, {16 + i % 4 * 250, 344 + i / 4 * 84, 242, 76}, label, value, id);
}

void draw_stats_grid(Painter &p)
{
    p.fill(theme->bg);
    const asr::Stats s = asr::stats();
    transcript::lock();
    const transcript::Delays d = transcript::delays();
    transcript::unlock();
    char v[64];
    snprintf(v, sizeof v, s.chunks ? "%d ms" : "-", (int)s.mean_ms);
    number_card(p, 0, "compute per chunk (mean)", v, "num.mean");
    snprintf(v, sizeof v, s.chunks ? "%d / %d ms" : "-", (int)s.median_ms, (int)s.p95_ms);
    number_card(p, 1, "median / p95", v, "num.median");
    snprintf(v, sizeof v, s.chunks ? "%d ms" : "-", (int)s.max_ms);
    number_card(p, 2, "slowest chunk", v, "num.max");
    snprintf(v, sizeof v, s.chunks ? "%.2f x" : "-", s.x_real_time);
    number_card(p, 3, "compute / audio", v, "num.rtf");
    snprintf(v, sizeof v, "%.1f s", s.behind_ms / 1000.0);
    number_card(p, 4, "behind live", v, "num.behind");
    snprintf(v, sizeof v, "%d / %u", d.n, (unsigned)s.tokens);
    number_card(p, 5, "words / tokens", v, "num.words");
    if (d.n)
        snprintf(v, sizeof v, "%.1f / %.1f s", d.median / 1000.0, d.p95 / 1000.0);
    else
        snprintf(v, sizeof v, "-");
    number_card(p, 6, "word delay median / p95", v, "num.delay");
    const double minutes = s.listening_ms / 60000.0;
    snprintf(v, sizeof v, minutes > 0.05 ? "%.0f" : "-", d.n / std::max(minutes, 0.05));
    number_card(p, 7, "words per minute", v, "num.wpm");
    char t1[16];
    fmt_time(t1, sizeof t1, s.listening_ms);
    number_card(p, 8, "audio listened", t1, "num.audio");
    snprintf(v, sizeof v, "%u / %u", (unsigned)s.frames_lost, (unsigned)s.clipped);
    number_card(p, 9, "frames lost / clipped samples", v, "num.lost");
    snprintf(v, sizeof v, "%u / %u KB", (unsigned)(s.psram_free / 1024), (unsigned)(s.psram_min / 1024));
    number_card(p, 10, "PSRAM free / lowest", v, "num.psram");
    fmt_time(t1, sizeof t1, static_cast<int32_t>(s.uptime_us / 1000));
    number_card(p, 11, "up for", t1, "num.uptime");
}

} // namespace ui
