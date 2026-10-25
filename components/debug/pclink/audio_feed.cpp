// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * A stream from the PC instead of the microphone, for tests that compare the board with the PC:
 *
 *   feed_begin          from IDLE: "ok feed_begin"
 *   feed <samples>      "ok send", then that many 16 kHz int16 samples (at most 1 s): "ok fed <samples>"
 *   feed_end            the stream's true end: "ok feed <chunks> <crc> <ms per chunk> <features> <encoder> <search>
 *                       | text || pieces" (the parts' means over the chunks kept)
 */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include "asr_service.hpp"
#include "esp_heap_caps.h"
#include "pclink.hpp"
#include "pclink_internal.hpp"
#include "transcript.hpp"

namespace pclink {

static constexpr int FEED_MAX = 16000;
static int16_t *s_buf;

void cmd_feed_begin(int, char **)
{
    const asr::State st = asr::state();
    if (st != asr::IDLE) {
        reply("err feed: %s", asr::state_name(st));
        return;
    }
    if (!s_buf)
        s_buf = static_cast<int16_t *>(heap_caps_malloc(FEED_MAX * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    if (!s_buf || !asr::feed_begin()) {
        reply("err feed: %s", s_buf ? "not idle" : "no memory");
        return;
    }
    send_words(true);
    reply("ok feed_begin");
}

void cmd_feed(int, char **argv)
{
    const int n = atoi(argv[1]);
    if (!asr::feeding() || n <= 0 || n > FEED_MAX) {
        reply("err feed");
        return;
    }
    if (!receive(s_buf, sizeof(int16_t) * n, 10000)) {
        reply("err feed: short");
        return;
    }
    if (!asr::feed(s_buf, n)) {
        reply("err feed: the ring overran");
        return;
    }
    reply("ok fed %d", n);
}

static void feed_end_reply()
{
    const size_t cap = 8192;
    char *text = static_cast<char *>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM));
    if (!text) {
        reply("err feed_end: no memory");
        return;
    }
    const asr::Stats s = asr::stats();
    static asr::Chunk c[240];
    int k = asr::recent_chunks(c, 240);
    double f = 0, e = 0, g = 0;
    for (int i = 0; i < k; i++) {
        f += c[i].fbank_ms;
        e += c[i].encode_ms;
        g += c[i].search_ms;
    }
    k = k ? k : 1;
    size_t o = snprintf(text, cap, "ok feed %d %08lx %.1f %.1f %.1f %.1f | ", s.chunks,
                        (unsigned long)asr::session_crc(), s.chunks ? s.compute_us / 1000.0 / s.chunks : 0.0, f / k,
                        e / k, g / k);
    transcript::lock();
    for (int i = 0; i < transcript::words() && o + 2 < cap; i++) {
        const transcript::Word &w = transcript::word(i);
        o += snprintf(text + o, cap - o, "%s%s", w.space && i ? " " : "", transcript::text(w));
    }
    transcript::unlock();
    if (o + 2 < cap)
        o += snprintf(text + o, cap - o, " || %s", asr::session_pieces());
    o = std::min(o, cap - 1);
    printf("PC > %.*s\n", static_cast<int>(o), text);
    send("TXT0", text, static_cast<uint32_t>(o), nullptr, 0, true);
    heap_caps_free(text);
}

void cmd_feed_end(int, char **)
{
    if (!asr::feeding()) {
        reply("err feed_end");
        return;
    }
    const bool ok = asr::feed_end();
    heap_caps_free(s_buf);
    s_buf = nullptr;
    if (ok)
        feed_end_reply();
    else
        reply("err feed_end: the ring overran");
}

} // namespace pclink
