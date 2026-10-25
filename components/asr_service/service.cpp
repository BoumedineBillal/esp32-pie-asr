// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The service's state, its configuration and hooks, and the main task's loop.
 */
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "service_internal.hpp"
#include "transcript.hpp"

namespace asr {

Config g_config;
Hooks g_hooks;
std::atomic<State> g_state{WAITING_MODEL};
static char s_failure[120];
static std::atomic<bool> s_req_start{false}, s_req_stop{false}, s_req_clear{false}, s_req_save{false};
static std::atomic<uint32_t> s_saves{0};
static char s_save_result[96];
static bool s_save_ok;

void configure(const Config &c) { g_config = c; }
void set_hooks(const Hooks &h) { g_hooks = h; }
State state() { return g_state.load(); }
const char *failure() { return s_failure; }
void request_start() { s_req_start = true; }
void request_stop() { s_req_stop = true; }
void request_clear() { s_req_clear = true; }
void request_save() { s_req_save = true; }
uint32_t save_count() { return s_saves.load(); }
const char *save_result(bool *ok)
{
    if (ok)
        *ok = s_save_ok;
    return s_save_result;
}

const char *state_name(State s)
{
    switch (s) {
    case WAITING_MODEL: return "waiting for the model";
    case LOADING_MODEL: return "loading the model";
    case PREPARING: return "preparing";
    case IDLE: return "idle";
    case LISTENING: return "listening";
    case FINISHING: return "finishing";
    default: return "failed";
    }
}

void log(const char *fmt, ...)
{
    char s[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    printf("ASR %s\n", s);
    if (g_hooks.log)
        g_hooks.log(s);
}

void fail(const char *why)
{
    snprintf(s_failure, sizeof s_failure, "%s", why);
    g_state = FAILED;
    log("failed: %s", why);
}

void run()
{
    cache_autoload();
    if (!model_alloc())
        fail("no memory for the model's state");
    else
        load_from_card();
    for (;;) {
        State st = g_state.load();
        if (g_hooks.next_command) {
            char *line = g_hooks.next_command(st == LISTENING ? 0 : 20);
            if (line) {
                g_hooks.command(line);
                free(line);
                continue;
            }
        } else if (st != LISTENING) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        st = g_state.load();
        if (s_req_clear.exchange(false) && st != LISTENING && st != FINISHING)
            transcript::clear();
        if (s_req_save.exchange(false)) {
            s_save_ok = save_transcript(s_save_result, sizeof s_save_result);
            s_saves++;
        }
        if (s_req_start.exchange(false) && st == IDLE) {
            transcript::clear();
            session_start();
            g_state = LISTENING;
            log("listening");
        }
        if (s_req_stop.exchange(false) && g_state.load() == LISTENING && !feeding()) {
            g_state = FINISHING;
            log("finishing");
            if (!finish_stream())
                log("the ring dropped audio the stream needed");
            g_state = IDLE;
            const Stats s = stats();
            log("stopped: %d chunks, %.2f x real time, %u frames lost", s.chunks, s.x_real_time,
                (unsigned)s.frames_lost);
        }
        if (g_state.load() == LISTENING && !feeding()) {
            if (!run_ready_chunks()) {
                log("the ring dropped audio the stream needed: a new session from now");
                session_start();
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

bool feed_begin()
{
    if (g_state.load() != IDLE)
        return false;
    set_feeding(true);
    vTaskDelay(pdMS_TO_TICKS(100)); /* the audio task's last write done */
    transcript::clear();
    session_start();
    g_state = LISTENING;
    return true;
}

bool feed(const int16_t *pcm, int n)
{
    if (!feeding())
        return false;
    /* into the ring as the stream takes it (the ring holds 4 s; a chunk needs 1.4 s of it) */
    for (int done = 0; done < n;) {
        const int k = n - done < 4000 ? n - done : 4000;
        ring_push(pcm + done, k);
        done += k;
        if (!run_ready_chunks())
            return false;
    }
    return true;
}

bool feed_end()
{
    if (!feeding())
        return false;
    g_state = FINISHING;
    const bool ok = finish_stream();
    g_state = IDLE;
    set_feeding(false);
    return ok;
}

} // namespace asr
