// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The microphone (p4mic, 16 kHz) and the ring the stream reads: an audio task moves each 50 ms block into the ring
 * and measures its level, so the level keeps moving while a chunk is being computed. During a feed the fed samples go
 * into the ring instead.
 */
#include <cmath>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "p4mic.hpp"
#include "service_internal.hpp"

namespace asr {

static constexpr int BLOCK = 800; /* the audio task's read: 50 ms */

static p4mic::Microphone s_mic;
static p4mic::Config s_mic_cfg;
static std::atomic<float> s_gain_req{NAN};

/* the ring: written by the audio task (or by a feed), read by the stream; indices count samples since boot */
static int16_t *s_ring;
static std::atomic<uint64_t> s_w{0};
static std::atomic<bool> s_feeding{false};

/* the level */
static portMUX_TYPE s_level_lock = portMUX_INITIALIZER_UNLOCKED;
static Level s_level = {-120, -120, false};
static int64_t s_peak_until, s_clip_until;
static std::atomic<uint32_t> s_lost{0}, s_clipped{0};

void set_gain(float db) { s_gain_req = db; }
void set_feeding(bool on) { s_feeding = on; }
bool feeding() { return s_feeding.load(); }
uint32_t frames_lost() { return s_lost.load(); }
uint32_t clipped() { return s_clipped.load(); }
float gain_now() { return s_mic.isOpen() ? s_mic.gain() : s_mic_cfg.gain_db; }
uint64_t ring_written() { return s_w.load(); }

Level level()
{
    portENTER_CRITICAL(&s_level_lock);
    Level l = s_level;
    portEXIT_CRITICAL(&s_level_lock);
    return l;
}

static void audio_task(void *)
{
    static int16_t buf[BLOCK];
    for (;;) {
        const float g = s_gain_req.exchange(NAN);
        if (!std::isnan(g)) {
            s_mic.setGain(g);
            log("gain %.1f dB", s_mic.gain());
        }
        p4mic::BlockInfo bi;
        const int n = s_mic.read(buf, BLOCK, &bi, 200);
        if (n <= 0)
            continue;
        s_lost += bi.lost;
        double sq = 0;
        int pk = 0, clip = 0;
        for (int i = 0; i < n; i++) {
            sq += static_cast<double>(buf[i]) * buf[i];
            const int a = buf[i] < 0 ? -buf[i] : buf[i];
            pk = a > pk ? a : pk;
            clip += a >= 32767;
        }
        s_clipped += clip;
        if (g_hooks.audio)
            g_hooks.audio(buf, n);
        const int64_t now = esp_timer_get_time();
        const float rms = 10 * log10f(static_cast<float>(sq / n) / (32768.0f * 32768.0f) + 1e-12f);
        const float pdb = 20 * log10f(pk / 32768.0f + 1e-9f);
        portENTER_CRITICAL(&s_level_lock);
        s_level.rms_db = rms;
        if (pdb >= s_level.peak_db || now > s_peak_until) {
            s_level.peak_db = pdb;
            s_peak_until = now + 1000000;
        }
        if (clip)
            s_clip_until = now + 1000000;
        s_level.clipping = now < s_clip_until;
        portEXIT_CRITICAL(&s_level_lock);
        if (!s_feeding)
            ring_push(buf, n);
    }
}

bool audio_alloc()
{
    s_ring = static_cast<int16_t *>(heap_caps_calloc(RING, sizeof(int16_t), MALLOC_CAP_SPIRAM));
    return s_ring != nullptr;
}

bool audio_open()
{
    s_mic_cfg.sample_rate = RATE;
    s_mic_cfg.buffer_ms = 1000; /* the ring here holds the stream's audio; p4mic's only bridges the audio task */
    s_mic_cfg.task_core = 0;
    if (!s_mic.open(s_mic_cfg))
        return false;
    /* core 0 above the main task: the microphone never waits for a chunk */
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, nullptr, 7, nullptr, 0);
    return true;
}

void ring_push(const int16_t *x, int n)
{
    const uint64_t w = s_w.load();
    for (int i = 0; i < n; i++)
        s_ring[(w + i) & (RING - 1)] = x[i];
    s_w.store(w + n);
}

bool ring_copy(int16_t *dst, uint64_t from, int n)
{
    if (s_w.load() - from > RING)
        return false;
    for (int i = 0; i < n; i++)
        dst[i] = s_ring[(from + i) & (RING - 1)];
    return true;
}

} // namespace asr
