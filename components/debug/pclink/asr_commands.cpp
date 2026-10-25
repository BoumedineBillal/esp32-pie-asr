// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The service's commands (text lines; replies "ok ..." / "err ..."):
 *
 *   hello                       "ok <app> 1 <state>" (the reason when failed, where the model came from)
 *   info                        memory
 *   load <bytes> <crc32>        "ok send", then the weights (a board without them on its SD card)
 *   start | stop | clear        as the screen's buttons
 *   gain <db>                   the microphone's gain
 *   stats                       one line of numbers
 *   words                       the transcript as WRD0 messages, then "ok words <n>"
 *   reboot
 *   feed_begin, feed <samples>, feed_end     a stream from the PC instead of the microphone (audio_feed.cpp)
 *   toks                        the last session's committed tokens: "ok toks <frames> | frame:id ..."
 *   vocab                       the model's pieces: "ok vocab <n> |" and "\n" before each id's piece
 *   rules <timeout s> <pause s> the word rules (not kept)
 *   tap 1 | 0                   the microphone's 16 kHz blocks to the PC as AUD0 messages too
 *   phrases [on | off]          the phrases' list and switch; phrase <index> 1 | 0: one line
 *   save                        the transcript to the SD card: "ok save <file>"
 *   sd_list                     the SD card's top folder: "ok sd | name size; ..."
 *   sd_get <file>               a text file from the SD card (at most 64 KB): "ok sd_get <n> |" then its bytes
 *   sd_copy <from> <to>         a file copied on the SD card, with its CRC
 *   sd_rm <file>                a file removed from the SD card
 *
 * While the PC listens (a ping in the last 3 s), every final word goes to it as a WRD0 message, "<word> <line> <end
 * ms> <final ms> <space> text": the word and line numbers count from the session's start.
 */
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include "asr_service.hpp"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "p4sdcard.hpp"
#include "pclink.hpp"
#include "pclink_internal.hpp"
#include "phrases.hpp"
#include "transcript.hpp"

namespace pclink {

const char *g_app_name = "asr";
static std::atomic<bool> s_tap{false};
static uint32_t s_words_sent; /* the session's words already sent (counting the ones dropped since) */
static uint32_t s_gen, s_dropped; /* the transcript's generation and dropped words when last sent */

bool tapping() { return s_tap.load(); }

void send_words(bool from_start)
{
    if (from_start)
        s_words_sent = 0;
    if (!pc_listening())
        return;
    transcript::lock();
    const uint32_t dw = transcript::dropped_words(), dl = transcript::dropped_lines();
    const uint32_t n = dw + static_cast<uint32_t>(transcript::words());
    const uint32_t gen = transcript::generation();
    if (gen != s_gen && dw <= s_dropped)
        s_words_sent = 0; /* a new generation without a drop: cleared */
    s_gen = gen;
    s_dropped = dw;
    if (s_words_sent > n || s_words_sent < dw)
        s_words_sent = dw;
    for (; s_words_sent < n; s_words_sent++) {
        const transcript::Word &w = transcript::word(static_cast<int>(s_words_sent - dw));
        char line[480];
        int k = snprintf(line, sizeof line, "%u %u %d %d %d %s", (unsigned)s_words_sent, (unsigned)(dl + w.line),
                         (int)w.end_ms, (int)w.final_ms, w.space ? 1 : 0, transcript::text(w));
        send("WRD0", line, static_cast<uint32_t>(k > 0 ? k : 0));
    }
    transcript::unlock();
}

static void hello(int, char **)
{
    const asr::State st = asr::state();
    const char *src = asr::model_source();
    reply("ok %s 1 %s%s%s%s%s", g_app_name, asr::state_name(st), st == asr::FAILED ? ": " : "",
          st == asr::FAILED ? asr::failure() : "", *src ? ", the model from " : "", src);
}

static void info(int, char **)
{
    reply("ok info psram free %u largest %u | internal free %u largest %u",
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

static void start(int, char **)
{
    asr::request_start();
    reply("ok start");
}

static void stop(int, char **)
{
    asr::request_stop();
    reply("ok stop");
}

static void clear(int, char **)
{
    asr::request_clear();
    reply("ok clear");
}

static void gain(int, char **argv)
{
    asr::set_gain(static_cast<float>(atof(argv[1])));
    reply("ok gain");
}

static void stats(int, char **)
{
    const asr::Stats s = asr::stats();
    const transcript::Delays d = transcript::delays();
    reply("ok stats chunks %d listening %d behind %d x_rt %.3f mean %d median %d p95 %d max %d lost %u clipped %u "
          "tokens %u words %d delay_median %d delay_p95 %d psram %u psram_min %u internal %u gain %.1f",
          s.chunks, (int)s.listening_ms, (int)s.behind_ms, s.x_real_time, (int)s.mean_ms, (int)s.median_ms,
          (int)s.p95_ms, (int)s.max_ms, (unsigned)s.frames_lost, (unsigned)s.clipped, (unsigned)s.tokens, d.n,
          (int)d.median, (int)d.p95, (unsigned)s.psram_free, (unsigned)s.psram_min, (unsigned)s.internal_free,
          s.gain_db);
}

static void words(int, char **)
{
    send_words(true);
    reply("ok words %d", transcript::words());
}

static void reboot(int, char **)
{
    reply("ok reboot");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

static void toks(int, char **)
{
    char head[48];
    const int n = snprintf(head, sizeof head, "ok toks %d |", asr::session_frames());
    size_t len;
    const char *t = asr::session_tokens(&len);
    send("TXT0", head, static_cast<uint32_t>(n), t, static_cast<uint32_t>(len), true);
}

static void vocab(int, char **)
{
    const char *const *v = asr::vocabulary();
    if (!v) {
        reply("err vocab: no model");
        return;
    }
    constexpr int N = 650;
    size_t total = 0;
    for (int i = 0; i < N; i++)
        total += (v[i] ? strlen(v[i]) : 0) + 1;
    char *out = static_cast<char *>(heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM));
    if (!out) {
        reply("err vocab: no memory");
        return;
    }
    size_t o = 0;
    for (int i = 0; i < N; i++)
        o += snprintf(out + o, total + 1 - o, "\n%s", v[i] ? v[i] : "");
    char head[32];
    const int n = snprintf(head, sizeof head, "ok vocab %d |", N);
    send("TXT0", head, static_cast<uint32_t>(n), out, static_cast<uint32_t>(o), true);
    heap_caps_free(out);
}

static void rules(int, char **argv)
{
    transcript::set_rules(static_cast<float>(atof(argv[1])), static_cast<float>(atof(argv[2])));
    reply("ok rules");
}

static void tap(int, char **argv)
{
    s_tap = atoi(argv[1]) != 0;
    reply("ok tap %d", s_tap.load() ? 1 : 0);
}

/* "phrases": the list (index:on|off:shown, then its spoken forms) */
static void phrases_cmd(int argc, char **argv)
{
    if (argc == 2)
        phrases::set_on(!strcmp(argv[1], "on"));
    constexpr size_t CAP = 4096;
    char *out = static_cast<char *>(heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM));
    if (!out) {
        reply("err phrases: no memory");
        return;
    }
    int o = snprintf(out, CAP, "ok phrases %s, from %s: %d words, %d sentences, %d ignored |",
                     phrases::on() ? "on" : "off", phrases::source(), phrases::words(), phrases::sentences(),
                     phrases::ignored());
    for (int col = 0; col < 2; col++) {
        const int n = col ? phrases::sentences() : phrases::words();
        for (int j = 0; j < n && o < (int)CAP - 200; j++) {
            const int i = col ? phrases::sentence(j) : phrases::word(j);
            const pieasr_phrase_t &it = phrases::item(i);
            o += snprintf(out + o, CAP - o, " %d:%s:%s", i, phrases::enabled(i) ? "on" : "off", it.shown);
            for (int f = 1; f < it.nforms && o < (int)CAP - 200; f++)
                o += snprintf(out + o, CAP - o, "=%s", it.forms[f]);
            o += snprintf(out + o, CAP - o, ";");
        }
    }
    o = std::min(o, (int)CAP - 1);
    send("TXT0", out, static_cast<uint32_t>(o), nullptr, 0, true); /* (longer than reply's 400 bytes) */
    heap_caps_free(out);
}

static void phrase_cmd(int, char **argv)
{
    phrases::set_enabled(atoi(argv[1]), atoi(argv[2]) != 0);
    reply("ok phrase %d %s", atoi(argv[1]), atoi(argv[2]) ? "on" : "off");
}

static void save(int, char **)
{
    char name[96];
    if (asr::save_transcript(name, sizeof name))
        reply("ok save %s", name);
    else
        reply("err save: %s", name);
}

static void sd_list(int, char **)
{
    p4sdcard::Card sd;
    if (!sd.open()) {
        reply("err sd: no card");
        return;
    }
    static char out[1024];
    int o = snprintf(out, sizeof out, "ok sd |");
    sd.list(
        [](const char *name, long size, void *arg) {
            int &o = *static_cast<int *>(arg);
            if (o < (int)sizeof out - 80)
                o += snprintf(out + o, sizeof out - o, " %s %ld;", name, size);
        },
        &o);
    sd.close();
    reply("%s", out);
}

static void sd_get(int, char **argv)
{
    p4sdcard::Card sd;
    if (!sd.open()) {
        reply("err sd: no card");
        return;
    }
    const long n = sd.fileSize(argv[1]);
    char *buf = n >= 0 && n <= 65536 ? static_cast<char *>(heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM)) : nullptr;
    const long r = buf ? sd.read(argv[1], buf, n) : -1;
    sd.close();
    if (r != n || n < 0) {
        heap_caps_free(buf);
        reply("err sd_get %s", argv[1]);
        return;
    }
    char head[48];
    const int h = snprintf(head, sizeof head, "ok sd_get %ld |", n);
    send("TXT0", head, static_cast<uint32_t>(h), buf, static_cast<uint32_t>(n), true);
    heap_caps_free(buf);
}

static void sd_copy(int, char **argv)
{
    p4sdcard::Card sd;
    if (!sd.open()) {
        reply("err sd: no card");
        return;
    }
    uint32_t crc = 0;
    const bool ok = sd.copy(argv[1], argv[2], &crc);
    const long n = sd.fileSize(argv[2]);
    sd.close();
    if (ok)
        reply("ok sd_copy %ld bytes, crc %08x", n, (unsigned)crc);
    else
        reply("err sd_copy");
}

static void sd_rm(int, char **argv)
{
    p4sdcard::Card sd;
    if (!sd.open()) {
        reply("err sd: no card");
        return;
    }
    const bool ok = sd.remove(argv[1]);
    sd.close();
    if (ok)
        reply("ok sd_rm %s", argv[1]);
    else
        reply("err sd_rm %s", argv[1]);
}

const Command ASR_COMMANDS[] = {
    {"hello", 1, hello},         {"info", 1, info},           {"load", 3, cmd_load},     {"start", 1, start},
    {"stop", 1, stop},           {"clear", 1, clear},         {"gain", 2, gain},         {"stats", 1, stats},
    {"words", 1, words},         {"reboot", 1, reboot},       {"feed_begin", 1, cmd_feed_begin},
    {"feed", 2, cmd_feed},       {"feed_end", 1, cmd_feed_end}, {"toks", 1, toks},     {"vocab", 1, vocab},
    {"rules", 3, rules},         {"tap", 2, tap},             {"phrases", 1, phrases_cmd},
    {"phrases", 2, phrases_cmd}, {"phrase", 3, phrase_cmd},   {"save", 1, save},         {"sd_list", 1, sd_list},
    {"sd_get", 2, sd_get},       {"sd_copy", 3, sd_copy},     {"sd_rm", 2, sd_rm},
};
const int ASR_COMMANDS_N = sizeof ASR_COMMANDS / sizeof ASR_COMMANDS[0];

} // namespace pclink
