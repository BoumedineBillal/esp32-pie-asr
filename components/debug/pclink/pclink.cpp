// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * pclink.hpp: usb_stream underneath; the received bytes are split here (in TinyUSB's task) into command lines (a
 * queue) or a binary payload (written straight to its destination); the commands are dispatched in the main task.
 */
#include "pclink.hpp"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "asr_service.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "usb_stream.hpp"

namespace pclink {

static QueueHandle_t s_lines;
static SemaphoreHandle_t s_send_lock, s_bin_done;
static uint8_t *s_bin;
static std::atomic<size_t> s_bin_left{0}, s_bin_got{0};
static std::atomic<int64_t> s_ping_us{0};
static const Command *s_app;
static int s_app_n;

static void on_rx(const uint8_t *d, size_t n)
{
    static char line[160];
    static size_t len;
    size_t i = 0;
    while (i < n) {
        size_t left = s_bin_left.load();
        if (left) {
            size_t k = n - i < left ? n - i : left;
            memcpy(s_bin, d + i, k);
            s_bin += k;
            i += k;
            s_bin_got += k;
            if (s_bin_left.fetch_sub(k) == k)
                xSemaphoreGive(s_bin_done);
            continue;
        }
        char c = static_cast<char>(d[i++]);
        if (c == '\r')
            continue;
        if (c != '\n') {
            if (len < sizeof line - 1)
                line[len++] = c;
            continue;
        }
        line[len] = 0;
        len = 0;
        if (!strcmp(line, "ping")) { /* the PC app is there: not a command */
            s_ping_us = esp_timer_get_time();
            continue;
        }
        char *copy = strdup(line);
        if (copy && xQueueSend(s_lines, &copy, 0) != pdTRUE)
            free(copy);
    }
}

bool pc_listening()
{
    int64_t t = s_ping_us.load();
    return t && esp_timer_get_time() - t < 3000000 && usb_stream::mounted();
}

void send(const char magic[4], const void *a, uint32_t na, const void *b, uint32_t nb, bool force)
{
    if (!force && !pc_listening())
        return;
    uint8_t h[8];
    uint32_t len = na + nb;
    memcpy(h, magic, 4);
    memcpy(h + 4, &len, 4);
    xSemaphoreTake(s_send_lock, portMAX_DELAY);
    usb_stream::send(h, 8);
    if (na)
        usb_stream::send(a, na);
    if (nb)
        usb_stream::send(b, nb);
    xSemaphoreGive(s_send_lock);
}

void reply(const char *fmt, ...)
{
    char s[400];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    n = n < 0 ? 0 : n >= (int)sizeof s ? (int)sizeof s - 1 : n;
    printf("PC > %s\n", s);
    send("TXT0", s, static_cast<uint32_t>(n), nullptr, 0, true);
}

void log(const char *fmt, ...)
{
    char s[400];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    n = n < 0 ? 0 : n >= (int)sizeof s ? (int)sizeof s - 1 : n;
    printf("PC %s\n", s);
    send("LOG0", s, static_cast<uint32_t>(n));
}

bool receive(void *dst, size_t n, uint32_t timeout_ms)
{
    xSemaphoreTake(s_bin_done, 0);
    s_bin = static_cast<uint8_t *>(dst);
    s_bin_got = 0;
    s_bin_left = n;
    reply("ok send");
    if (xSemaphoreTake(s_bin_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        s_bin_left = 0;
        return false;
    }
    return true;
}

size_t received() { return s_bin_got.load(); }

void add_commands(const Command *c, int n)
{
    s_app = c;
    s_app_n = n;
}

/* ---- the service's hooks */
static char *next_command(uint32_t timeout_ms)
{
    char *line = nullptr;
    if (xQueueReceive(s_lines, &line, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        return nullptr;
    return line;
}

static void command(char *line)
{
    char *argv[6];
    int argc = 0;
    char *save = nullptr;
    for (char *t = strtok_r(line, " ", &save); t && argc < 6; t = strtok_r(nullptr, " ", &save))
        argv[argc++] = t;
    if (!argc)
        return;
    for (int i = 0; i < ASR_COMMANDS_N; i++)
        if (!strcmp(argv[0], ASR_COMMANDS[i].name) && argc == ASR_COMMANDS[i].argc) {
            ASR_COMMANDS[i].run(argc, argv);
            return;
        }
    for (int i = 0; i < s_app_n; i++)
        if (!strcmp(argv[0], s_app[i].name) && argc == s_app[i].argc) {
            s_app[i].run(argc, argv);
            return;
        }
    reply("err unknown: %s", argv[0]);
}

static void log_line(const char *line) { send("LOG0", line, static_cast<uint32_t>(strlen(line))); }

static void audio(const int16_t *pcm, int n)
{
    if (tapping() && pc_listening())
        send("AUD0", pcm, static_cast<uint32_t>(sizeof(int16_t) * n));
}

static void chunk_done() { send_words(false); }

extern const char *g_app_name;

void attach(const char *app)
{
    g_app_name = app;
    s_lines = xQueueCreate(16, sizeof(char *));
    s_send_lock = xSemaphoreCreateMutex();
    s_bin_done = xSemaphoreCreateBinary();
    usb_stream::start(on_rx);
    asr::Hooks h;
    h.next_command = next_command;
    h.command = command;
    h.log = log_line;
    h.audio = audio;
    h.chunk_done = chunk_done;
    asr::set_hooks(h);
}

} // namespace pclink
