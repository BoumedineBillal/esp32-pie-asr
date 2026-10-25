// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Microphone (p4mic.hpp): the I2S receive channel (P4 master, Philips, two slots of 16 or 32 bits, MCLK 256 x Fs),
 * the codec (es8311.cpp) on the board's I2C bus, the capture task and its ring (int16 or int32 frames).
 *
 * The I2S driver fills 8 DMA buffers of 480 frames (30 ms at 16 kHz) and queues up to 7 finished ones; when the queue
 * is full and another finishes, it drops the oldest (its on_recv_q_ovf: i2s_common.c, IDF 6.1), so the task puts 480
 * zeros in its place: the ring's frame indices stay
 * the I2S's, and a frame's time is known from the receive interrupts (the last one's time and frame count, at the
 * nominal rate). The ring keeps the left slot (both slots carry the ADC: register 0x44 = 0x08). The reader keeps
 * DMA_FRAMES x 4 frames away from the writer: a reader further behind skips forward and counts the frames as lost.
 */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "es8311.hpp"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "p4mic.hpp"

namespace p4mic {

static const char *TAG = "p4mic";
static constexpr int DMA_BUFFERS = 8;
static constexpr int DMA_FRAMES = 480;                 /* 30 ms at 16 kHz; 3840 bytes at 32-bit (the limit: 4092) */
static constexpr uint64_t MARGIN = DMA_FRAMES * 4;     /* a reader stays this far from the writer */
static constexpr int LOSS_EVENTS = 16;

struct LossEvent {
    uint64_t index;    /* the first zero frame */
    uint32_t frames;
};

struct GainEvent {
    uint64_t index;    /* the frame being sampled when the codec's registers were written */
    float db;
};
static constexpr int GAIN_EVENTS = 32;

struct Microphone::State {
    Config cfg;
    i2c_master_bus_handle_t bus = nullptr;
    bool own_bus = false;
    detail::Es8311 codec;
    bool codec_attached = false, codec_started = false;
    i2s_chan_handle_t rx = nullptr, tx = nullptr;
    /* the receive interrupts */
    portMUX_TYPE isr_lock = portMUX_INITIALIZER_UNLOCKED;
    uint64_t dma_frames = 0, frames_at_first = 0; /* under isr_lock */
    int64_t first_us = 0, last_us = 0;
    std::atomic<uint32_t> overflows{0};
    /* the ring: frames [w - cap, w) are in it, at index % cap; int16 (bits 16) or int32 (bits 24) */
    void *ring = nullptr;
    bool wide = false;
    uint64_t cap = 0;
    std::atomic<uint64_t> w{0};
    void *dma_copy = nullptr;                      /* one I2S buffer, internal RAM */
    int frame_bytes = 4;                           /* an I2S frame: two slots of 16 or 32 bits */
    /* the losses: the I2S's (zeros in the ring) and the reader's (skipped) */
    portMUX_TYPE loss_lock = portMUX_INITIALIZER_UNLOCKED;
    LossEvent loss[LOSS_EVENTS] = {};
    int nloss = 0;
    std::atomic<uint64_t> lost_total{0};
    /* the gain, and when each change was made (BlockInfo::gain_db) */
    SemaphoreHandle_t ctl = nullptr;               /* the codec's registers from the API's tasks */
    StaticSemaphore_t ctl_mem;
    GainStages gain;
    portMUX_TYPE gain_lock = portMUX_INITIALIZER_UNLOCKED;
    GainEvent glog[GAIN_EVENTS] = {};
    int nglog = 0;
    /* the reader */
    SemaphoreHandle_t reader = nullptr, data = nullptr;
    StaticSemaphore_t reader_mem, data_mem;
    uint64_t r = 0, lost_pending = 0;
    /* the task */
    TaskHandle_t task = nullptr;
    std::atomic<bool> stop{false};
    SemaphoreHandle_t stopped = nullptr;
    StaticSemaphore_t stopped_mem;
};

static bool IRAM_ATTR on_recv(i2s_chan_handle_t, i2s_event_data_t *e, void *ctx)
{
    auto *s = static_cast<Microphone::State *>(ctx);
    int64_t t = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s->isr_lock);
    s->dma_frames += e->size / s->frame_bytes;
    if (s->first_us == 0) {
        s->first_us = t;
        s->frames_at_first = s->dma_frames;
    }
    s->last_us = t;
    portEXIT_CRITICAL_ISR(&s->isr_lock);
    return false;
}

static bool IRAM_ATTR on_overflow(i2s_chan_handle_t, i2s_event_data_t *, void *ctx)
{
    static_cast<Microphone::State *>(ctx)->overflows++;
    return false;
}

/* frames into the ring at w (from the I2S buffer's left slots, or zeros), then w moves on */
template <typename T> static void put_as(Microphone::State *s, const T *lr, uint64_t frames)
{
    uint64_t w = s->w.load(std::memory_order_relaxed);
    T *ring = static_cast<T *>(s->ring);
    for (uint64_t done = 0; done < frames;) {
        uint64_t pos = (w + done) % s->cap, n = std::min(frames - done, s->cap - pos);
        T *d = ring + pos;
        if (lr) {
            const T *src = lr + 2 * done;
            for (uint64_t i = 0; i < n; i++)
                d[i] = src[2 * i];
        } else {
            memset(d, 0, n * sizeof(T));
        }
        done += n;
    }
    s->w.store(w + frames, std::memory_order_release);
}

static void put(Microphone::State *s, const void *lr, uint64_t frames)
{
    if (s->wide)
        put_as(s, static_cast<const int32_t *>(lr), frames);
    else
        put_as(s, static_cast<const int16_t *>(lr), frames);
}

static void capture_task(void *arg)
{
    auto *s = static_cast<Microphone::State *>(arg);
    uint32_t ovf_seen = 0;
    while (!s->stop) {
        size_t got = 0;
        esp_err_t e = i2s_channel_read(s->rx, s->dma_copy, DMA_FRAMES * s->frame_bytes, &got, 50);
        /* buffers the driver dropped: older than the one just taken (it drops the oldest) */
        uint32_t ovf = s->overflows.load();
        if (ovf != ovf_seen) {
            uint32_t n = (ovf - ovf_seen) * DMA_FRAMES;
            portENTER_CRITICAL(&s->loss_lock);
            s->loss[s->nloss % LOSS_EVENTS] = {s->w.load(), n};
            s->nloss++;
            portEXIT_CRITICAL(&s->loss_lock);
            s->lost_total += n;
            put(s, nullptr, n);
            ovf_seen = ovf;
        }
        if (e != ESP_OK || got < static_cast<size_t>(s->frame_bytes))
            continue;
        put(s, s->dma_copy, got / s->frame_bytes);
        xSemaphoreGive(s->data);
    }
    xSemaphoreGive(s->stopped);
    vTaskDelete(nullptr);
}

#define MCHECK(x, what)                                                                                              \
    do {                                                                                                             \
        if ((err_ = (x)) != ESP_OK) {                                                                                \
            ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err_));                                                    \
            close();                                                                                                 \
            return false;                                                                                            \
        }                                                                                                            \
    } while (0)

bool Microphone::open(const Config &c)
{
    if (isOpen())
        return true;
    if (!detail::Es8311::rateSupported(c.sample_rate) || (c.bits != 16 && c.bits != 24)) {
        ESP_LOGE(TAG, "sample rate %d Hz, %d bits: 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100 or 48000 Hz; "
                      "16 or 24 bits", c.sample_rate, c.bits);
        err_ = ESP_ERR_NOT_SUPPORTED;
        return false;
    }
    /* the ring must hold more than the reader's margin and one I2S buffer: under that, read()'s checks would wrap */
    const uint64_t min_frames = MARGIN + DMA_FRAMES;
    if (c.buffer_ms < 200 || c.settle_ms < 0 ||
        static_cast<uint64_t>(c.sample_rate) * c.buffer_ms / 1000 < min_frames) {
        ESP_LOGE(TAG, "buffer %d ms (at least 200, and %u frames: %u ms at %d Hz), settle %d ms", c.buffer_ms,
                 (unsigned)min_frames, (unsigned)((min_frames * 1000 + c.sample_rate - 1) / c.sample_rate),
                 c.sample_rate, c.settle_ms);
        err_ = ESP_ERR_INVALID_ARG;
        return false;
    }
    st_ = new State();
    State *s = st_;
    s->cfg = c;
    s->reader = xSemaphoreCreateMutexStatic(&s->reader_mem);
    s->ctl = xSemaphoreCreateMutexStatic(&s->ctl_mem);
    s->data = xSemaphoreCreateBinaryStatic(&s->data_mem);
    s->stopped = xSemaphoreCreateBinaryStatic(&s->stopped_mem);
    s->wide = c.bits == 24;
    s->frame_bytes = s->wide ? 8 : 4;
    const size_t es = s->wide ? 4 : 2;
    s->cap = static_cast<uint64_t>(c.sample_rate) * c.buffer_ms / 1000;
    s->ring = heap_caps_malloc(s->cap * es, MALLOC_CAP_SPIRAM);
    s->dma_copy = heap_caps_malloc(DMA_FRAMES * s->frame_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s->ring || !s->dma_copy) {
        ESP_LOGE(TAG, "no memory for the ring (%u bytes of PSRAM)", (unsigned)(s->cap * es));
        err_ = ESP_ERR_NO_MEM;
        close();
        return false;
    }
    s->r = static_cast<uint64_t>(c.sample_rate) * c.settle_ms / 1000;

    /* the I2C bus: the one already on the port (the camera's), else a new one (the driver logs an error when it
     * finds none: silenced, it is not one here) */
    esp_log_level_t lv = esp_log_level_get("i2c.master");
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    esp_err_t have = i2c_master_get_bus_handle(static_cast<i2c_port_num_t>(c.i2c_port), &s->bus);
    esp_log_level_set("i2c.master", lv);
    if (have != ESP_OK) {
        i2c_master_bus_config_t bc = {};
        bc.i2c_port = c.i2c_port;
        bc.sda_io_num = static_cast<gpio_num_t>(c.sda_pin);
        bc.scl_io_num = static_cast<gpio_num_t>(c.scl_pin);
        bc.clk_source = I2C_CLK_SRC_DEFAULT;
        bc.glitch_ignore_cnt = 7;
        bc.flags.enable_internal_pullup = true;
        MCHECK(i2c_new_master_bus(&bc, &s->bus), "the I2C bus");
        s->own_bus = true;
    }
    MCHECK(s->codec.attach(s->bus, c.codec_address, c.i2c_hz), "the codec on the bus");
    s->codec_attached = true;
    int id1 = s->codec.read(0xFD), id2 = s->codec.read(0xFE);
    if (id1 != 0x83 || id2 != 0x11) {
        ESP_LOGE(TAG, "no ES8311 at 0x%02x (chip ID %02x %02x)", c.codec_address, id1 & 0xff, id2 & 0xff);
        err_ = ESP_ERR_NOT_FOUND;
        close();
        return false;
    }

    /* the I2S: running before the codec starts (its MCLK comes from here) */
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.id = static_cast<decltype(cc.id)>(c.i2s_port);
    cc.dma_desc_num = DMA_BUFFERS;
    cc.dma_frame_num = DMA_FRAMES;
    cc.auto_clear = true;
    MCHECK(i2s_new_channel(&cc, c.loopback ? &s->tx : nullptr, &s->rx), "the I2S channels");
    i2s_std_config_t sc = {};
    sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(static_cast<uint32_t>(c.sample_rate));
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(s->wide ? I2S_DATA_BIT_WIDTH_32BIT : I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO);
    sc.gpio_cfg.mclk = static_cast<gpio_num_t>(c.mclk_pin);
    sc.gpio_cfg.bclk = static_cast<gpio_num_t>(c.bclk_pin);
    sc.gpio_cfg.ws = static_cast<gpio_num_t>(c.ws_pin);
    sc.gpio_cfg.dout = c.loopback ? static_cast<gpio_num_t>(c.dout_pin) : I2S_GPIO_UNUSED;
    sc.gpio_cfg.din = static_cast<gpio_num_t>(c.din_pin);
    if (s->tx)
        MCHECK(i2s_channel_init_std_mode(s->tx, &sc), "the I2S transmit side");
    MCHECK(i2s_channel_init_std_mode(s->rx, &sc), "the I2S receive side");
    i2s_event_callbacks_t cbs = {};
    cbs.on_recv = on_recv;
    cbs.on_recv_q_ovf = on_overflow;
    MCHECK(i2s_channel_register_event_callback(s->rx, &cbs, s), "the I2S callbacks");

    /* the task first: it takes every I2S buffer from the first */
    BaseType_t ok = c.task_core < 0
                        ? xTaskCreate(capture_task, "p4mic", 3072, s, c.task_priority, &s->task)
                        : xTaskCreatePinnedToCore(capture_task, "p4mic", 3072, s, c.task_priority, &s->task,
                                                  c.task_core);
    if (ok != pdPASS) {
        s->task = nullptr;
        err_ = ESP_ERR_NO_MEM;
        close();
        return false;
    }
    if (s->tx)
        MCHECK(i2s_channel_enable(s->tx), "the I2S transmit side");
    MCHECK(i2s_channel_enable(s->rx), "the I2S receive side");

    /* the codec: Espressif's state (PGA +30 dB, scale 0, volume 0), then the gain asked for */
    MCHECK(s->codec.startAdc(c.sample_rate, s->wide ? 32 : 16), "the codec");
    s->codec_started = true;
    if (c.loopback)
        MCHECK(s->codec.startLoopback(), "the codec's loopback");
    if (!setGain(c.gain_db)) {
        ESP_LOGE(TAG, "the gain %.1f dB: %s", c.gain_db, esp_err_to_name(err_));
        close();
        return false;
    }
    return true;
}

void Microphone::close()
{
    State *s = st_;
    if (!s)
        return;
    if (s->task) {
        s->stop = true;
        xSemaphoreTake(s->stopped, portMAX_DELAY); /* the task's read times out within 50 ms */
    }
    if (s->codec_started)
        s->codec.stop();
    if (s->rx) {
        i2s_channel_disable(s->rx);
        i2s_del_channel(s->rx);
    }
    if (s->tx) {
        i2s_channel_disable(s->tx);
        i2s_del_channel(s->tx);
    }
    if (s->codec_attached)
        s->codec.detach();
    if (s->own_bus)
        i2c_del_master_bus(s->bus); /* refused (and kept) while another device is still on it */
    heap_caps_free(s->ring);
    heap_caps_free(s->dma_copy);
    delete s;
    st_ = nullptr;
}

/* when frame k reached the P4: from the last receive interrupt (its frame count and time), at the nominal rate */
static int64_t time_of(Microphone::State *s, uint64_t k)
{
    portENTER_CRITICAL(&s->isr_lock);
    uint64_t f = s->dma_frames;
    int64_t t = s->last_us;
    portEXIT_CRITICAL(&s->isr_lock);
    return t - static_cast<int64_t>((static_cast<double>(f) - 1 - static_cast<double>(k)) * 1e6 /
                                    s->cfg.sample_rate);
}

/* frames [r, r + n) of the ring into dst, in dst's width (16-bit: the top 16 bits; 32-bit: full scale +-2^31) */
template <typename T> static void copy_out(Microphone::State *s, uint64_t r, uint64_t n, T *dst)
{
    for (uint64_t done = 0; done < n;) {
        uint64_t pos = (r + done) % s->cap, m = std::min(n - done, s->cap - pos);
        T *d = dst + done;
        if (s->wide) {
            const int32_t *src = static_cast<const int32_t *>(s->ring) + pos;
            if (sizeof(T) == 4)
                memcpy(d, src, m * 4);
            else
                for (uint64_t i = 0; i < m; i++)
                    d[i] = static_cast<T>(src[i] >> 16);
        } else {
            const int16_t *src = static_cast<const int16_t *>(s->ring) + pos;
            if (sizeof(T) == 2)
                memcpy(d, src, m * 2);
            else
                for (uint64_t i = 0; i < m; i++)
                    d[i] = static_cast<T>(static_cast<int32_t>(src[i]) * 65536);
        }
        done += m;
    }
}

int Microphone::read(int16_t *samples, int frames, BlockInfo *info, uint32_t timeout_ms)
{
    return readAs(samples, frames, info, timeout_ms);
}

int Microphone::read(int32_t *samples, int frames, BlockInfo *info, uint32_t timeout_ms)
{
    return readAs(samples, frames, info, timeout_ms);
}

template <typename T> int Microphone::readAs(T *samples, int frames, BlockInfo *info, uint32_t timeout_ms)
{
    State *s = st_;
    if (!s || frames <= 0 || static_cast<uint64_t>(frames) > s->cap - MARGIN) {
        err_ = s ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
        return -1;
    }
    int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
    xSemaphoreTake(s->reader, portMAX_DELAY);
    uint64_t w, n;
    for (;;) {
        w = s->w.load(std::memory_order_acquire);
        if (w > s->cap - MARGIN && w - (s->cap - MARGIN) > s->r) { /* fell behind: skip to what is still safe */
            uint64_t skip = w - (s->cap - MARGIN) - s->r;
            s->r += skip;
            s->lost_pending += skip;
            s->lost_total += skip;
        }
        n = w > s->r ? w - s->r : 0;
        if (n >= static_cast<uint64_t>(frames))
            break;
        int64_t left = deadline - esp_timer_get_time();
        if (left <= 0)
            break;
        xSemaphoreGive(s->reader);
        xSemaphoreTake(s->data, pdMS_TO_TICKS(left / 1000 + 1));
        xSemaphoreTake(s->reader, portMAX_DELAY);
    }
    n = std::min<uint64_t>(n, frames);
    uint64_t r = s->r;
    copy_out(s, r, n, samples);
    uint32_t lost = static_cast<uint32_t>(s->lost_pending);
    portENTER_CRITICAL(&s->loss_lock); /* the I2S's zeros inside this block */
    int first = std::max(0, s->nloss - LOSS_EVENTS);
    for (int i = first; i < s->nloss; i++) {
        const LossEvent &ev = s->loss[i % LOSS_EVENTS];
        if (ev.index + ev.frames > r && ev.index < r + n)
            lost += static_cast<uint32_t>(std::min(ev.index + ev.frames, r + n) - std::max(ev.index, r));
    }
    portEXIT_CRITICAL(&s->loss_lock);
    if (info) {
        info->first = r;
        info->time_us = time_of(s, r);
        info->lost = lost;
        info->gain_db = s->gain.total();
        portENTER_CRITICAL(&s->gain_lock); /* the last change made before the block's first frame */
        for (int i = s->nglog - 1; i >= std::max(0, s->nglog - GAIN_EVENTS); i--)
            if (s->glog[i % GAIN_EVENTS].index <= r) {
                info->gain_db = s->glog[i % GAIN_EVENTS].db;
                break;
            }
        portEXIT_CRITICAL(&s->gain_lock);
    }
    s->r = r + n;
    s->lost_pending = 0;
    xSemaphoreGive(s->reader);
    err_ = n < static_cast<uint64_t>(frames) ? ESP_ERR_TIMEOUT : ESP_OK;
    return static_cast<int>(n);
}

int Microphone::available() const
{
    if (!st_)
        return 0;
    uint64_t w = st_->w.load(), r = st_->r;
    return w > r ? static_cast<int>(std::min<uint64_t>(w - r, st_->cap - MARGIN)) : 0;
}

void Microphone::flush()
{
    if (!st_)
        return;
    xSemaphoreTake(st_->reader, portMAX_DELAY);
    uint64_t w = st_->w.load();
    if (w > st_->r)
        st_->r = w;
    xSemaphoreGive(st_->reader);
}

/* the transmit side's frames are the receive side's width: int16_t pairs at bits 16, int32_t pairs at 24 */
static int write_loopback(Microphone::State *s, const void *lr, int frames, size_t frame_bytes, uint32_t timeout_ms,
                          esp_err_t &err)
{
    if (!s || !s->tx || frame_bytes != static_cast<size_t>(s->frame_bytes)) {
        err = s && s->tx ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
        return -1;
    }
    size_t done = 0;
    err = i2s_channel_write(s->tx, lr, frames * frame_bytes, &done, timeout_ms);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
        return -1;
    return static_cast<int>(done / frame_bytes);
}

int Microphone::writeLoopback(const int16_t *lr, int frames, uint32_t timeout_ms)
{
    return write_loopback(st_, lr, frames, 4, timeout_ms, err_);
}

int Microphone::writeLoopback(const int32_t *lr, int frames, uint32_t timeout_ms)
{
    return write_loopback(st_, lr, frames, 8, timeout_ms, err_);
}

/* ---- the gain ---- */

static float half_db(float db) { return std::round(db * 2) / 2; }

GainStages Microphone::split(float db)
{
    db = std::min(104.0f, std::max(-95.5f, half_db(db)));
    GainStages g;
    if (db <= 0) {
        g.pga_db = 0;
        g.scale_db = 0;
        g.volume_db = db;
        return g;
    }
    g.pga_db = std::min(30, static_cast<int>(std::floor(db / 3)) * 3);
    float rest = db - g.pga_db;
    g.scale_db = std::min(42, static_cast<int>(std::floor(rest / 6)) * 6);
    g.volume_db = rest - g.scale_db;
    return g;
}

bool Microphone::setGain(float db) { return setGainStages(split(db)); }

bool Microphone::setGainStages(const GainStages &want)
{
    State *s = st_;
    if (!s) {
        err_ = ESP_ERR_INVALID_STATE;
        return false;
    }
    GainStages g;
    g.pga_db = std::min(30, std::max(0, static_cast<int>(std::lround(want.pga_db / 3.0)) * 3));
    g.scale_db = std::min(42, std::max(0, static_cast<int>(std::lround(want.scale_db / 6.0)) * 6));
    g.volume_db = std::min(32.0f, std::max(-95.5f, half_db(want.volume_db)));
    xSemaphoreTake(s->ctl, portMAX_DELAY);
    /* 0x14: MIC1 to the PGA (LINSEL 1) and its gain in 3 dB steps; 0x16: the scale in 6 dB steps; 0x17: 0xBF = 0 dB,
     * 0.5 dB a step */
    esp_err_t e = s->codec.write(0x14, 0x10 | g.pga_db / 3);
    if (e == ESP_OK)
        e = s->codec.write(0x16, g.scale_db / 6);
    if (e == ESP_OK)
        e = s->codec.write(0x17, static_cast<int>(0xBF + g.volume_db * 2));
    if (e == ESP_OK) {
        s->gain = g;
        uint64_t now = frameNow();
        portENTER_CRITICAL(&s->gain_lock);
        s->glog[s->nglog % GAIN_EVENTS] = {now, g.total()};
        s->nglog++;
        portEXIT_CRITICAL(&s->gain_lock);
    }
    xSemaphoreGive(s->ctl);
    err_ = e;
    return e == ESP_OK;
}

/* the last receive interrupt's frame count plus the frames since it, at the nominal rate */
uint64_t Microphone::frameNow() const
{
    if (!st_)
        return 0;
    portENTER_CRITICAL(&st_->isr_lock);
    uint64_t f = st_->dma_frames;
    int64_t t = st_->last_us;
    portEXIT_CRITICAL(&st_->isr_lock);
    return f + (t ? static_cast<uint64_t>((esp_timer_get_time() - t) * st_->cfg.sample_rate / 1000000) : 0);
}

float Microphone::gain() const { return st_ ? st_->gain.total() : 0; }

GainStages Microphone::gainStages() const { return st_ ? st_->gain : GainStages(); }

int Microphone::codecRegister(int reg)
{
    if (!st_) {
        err_ = ESP_ERR_INVALID_STATE;
        return -1;
    }
    xSemaphoreTake(st_->ctl, portMAX_DELAY);
    int v = st_->codec.read(reg);
    xSemaphoreGive(st_->ctl);
    return v;
}

bool Microphone::setCodecRegister(int reg, int value)
{
    if (!st_) {
        err_ = ESP_ERR_INVALID_STATE;
        return false;
    }
    xSemaphoreTake(st_->ctl, portMAX_DELAY);
    err_ = st_->codec.write(reg, value);
    xSemaphoreGive(st_->ctl);
    return err_ == ESP_OK;
}

double Microphone::measuredRateHz() const
{
    if (!st_)
        return 0;
    portENTER_CRITICAL(&st_->isr_lock);
    uint64_t f = st_->dma_frames - st_->frames_at_first;
    int64_t dt = st_->last_us - st_->first_us;
    portEXIT_CRITICAL(&st_->isr_lock);
    return dt > 0 ? f * 1e6 / static_cast<double>(dt) : 0;
}

uint32_t Microphone::overflows() const { return st_ ? st_->overflows.load() : 0; }
uint64_t Microphone::lostFrames() const { return st_ ? st_->lost_total.load() : 0; }

} // namespace p4mic
