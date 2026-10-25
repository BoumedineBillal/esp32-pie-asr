// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The transcript to the SD card: one text line per transcript line, its start time first, in the next free
 * transcript_NNN.txt (the board has no clock, so the files are numbered).
 */
#include <cstdio>
#include "esp_heap_caps.h"
#include "p4sdcard.hpp"
#include "service_internal.hpp"
#include "transcript.hpp"

namespace asr {

static constexpr size_t CAP = 64 * 1024;

/* the transcript as text into buf; returns its length */
static size_t format(char *buf, size_t cap)
{
    size_t o = 0;
    transcript::lock();
    const int nw = transcript::words(), nl = transcript::lines();
    for (int l = 0; l < nl && o + 64 < cap; l++) {
        const uint32_t a = transcript::line(l).first_word;
        const uint32_t e = l + 1 < nl ? transcript::line(l + 1).first_word : static_cast<uint32_t>(nw);
        if (a >= e)
            continue;
        const int s = transcript::line(l).start_ms / 1000;
        o += snprintf(buf + o, cap - o, "[%d:%02d] ", s / 60, s % 60);
        for (uint32_t i = a; i < e && o + 2 < cap; i++) {
            const transcript::Word &w = transcript::word(static_cast<int>(i));
            o += snprintf(buf + o, cap - o, "%s%s", w.space && i > a ? " " : "", transcript::text(w));
        }
        if (o + 1 < cap)
            buf[o++] = '\n';
    }
    transcript::unlock();
    return o < cap ? o : cap - 1;
}

bool save_transcript(char *out, size_t cap)
{
    if (!transcript::words()) {
        snprintf(out, cap, "nothing to save");
        return false;
    }
    char *buf = static_cast<char *>(heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM));
    if (!buf) {
        snprintf(out, cap, "no memory");
        return false;
    }
    const size_t n = format(buf, CAP);
    p4sdcard::Card sd;
    bool ok = sd.open();
    char name[32] = "";
    if (ok) {
        int k = 1;
        for (; k < 1000; k++) {
            snprintf(name, sizeof name, "transcript_%03d.txt", k);
            if (sd.fileSize(name) < 0)
                break;
        }
        ok = k < 1000 && sd.write(name, buf, n);
        sd.close();
    }
    heap_caps_free(buf);
    if (ok)
        snprintf(out, cap, "%s", name);
    else
        snprintf(out, cap, "the microSD card could not be written");
    log("transcript %s: %s (%u bytes)", ok ? "saved" : "not saved", out, (unsigned)n);
    return ok;
}

} // namespace asr
