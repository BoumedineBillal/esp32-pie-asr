// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Touch (p4lcd.hpp): the GT911 touch controller, polled over I2C by a task of its own.
 *
 * The board wires neither the GT911's interrupt nor its reset line, so the chip keeps the address it took at power-up
 * (0x5D or 0x14) and is read on a timer. Its registers (16-bit addresses, big-endian): 0x8140 the product ID (4
 * ASCII bytes), 0x8144 the firmware, 0x8047 the config version, 0x8048 / 0x804A the configured X / Y resolution;
 * 0x814E the status (bit 7: a new report; bits 0-3: the number of points), cleared by writing 0; from 0x814F, 8 bytes
 * per point: track ID, X, Y, size (16-bit little-endian), reserved.
 */
#include <algorithm>
#include <atomic>
#include <cstring>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "p4lcd.hpp"

namespace p4lcd {

static const char *TAG = "p4lcd.touch";
static constexpr int MAX_POINTS = 5;
static constexpr int QUEUE = 32;
static constexpr int I2C_TIMEOUT_MS = 20;

struct Touch::State {
    TouchConfig cfg;
    i2c_master_bus_handle_t bus = nullptr;
    bool own_bus = false;
    i2c_master_dev_handle_t dev = nullptr;
    int addr = 0;
    char product[5] = {};
    int firmware = 0, config_version = 0, raw_w = 0, raw_h = 0;
    TaskHandle_t task = nullptr;
    std::atomic<bool> stop{false};
    SemaphoreHandle_t stopped = nullptr;
    QueueHandle_t events = nullptr;
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    TouchPoint now[MAX_POINTS];
    int count = 0;
    std::atomic<uint32_t> polls{0}, errors{0}, dropped{0};
    std::atomic<int64_t> bus_us{0};
};

static esp_err_t reg_read(i2c_master_dev_handle_t dev, uint16_t reg, uint8_t *buf, size_t n)
{
    uint8_t a[2] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg)};
    return i2c_master_transmit_receive(dev, a, 2, buf, n, I2C_TIMEOUT_MS);
}

static esp_err_t reg_write8(i2c_master_dev_handle_t dev, uint16_t reg, uint8_t v)
{
    uint8_t a[3] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg), v};
    return i2c_master_transmit(dev, a, 3, I2C_TIMEOUT_MS);
}

/* the chip's point in the picture's coordinates */
static void to_picture(const TouchConfig &c, int rw, int rh, int rx, int ry, int &x, int &y)
{
    if (c.swap_xy) {
        std::swap(rx, ry);
        std::swap(rw, rh);
    }
    int px = rw > 0 ? rx * c.width / rw : rx, py = rh > 0 ? ry * c.height / rh : ry;
    if (c.mirror_x)
        px = c.width - 1 - px;
    if (c.mirror_y)
        py = c.height - 1 - py;
    px = px < 0 ? 0 : px >= c.width ? c.width - 1 : px;
    py = py < 0 ? 0 : py >= c.height ? c.height - 1 : py;
    switch (c.rotation) { /* the inverse of the Display's picture -> panel mapping */
    case 90: x = py; y = c.width - 1 - px; break;
    case 180: x = c.width - 1 - px; y = c.height - 1 - py; break;
    case 270: x = c.height - 1 - py; y = px; break;
    default: x = px; y = py; break;
    }
}

static void push(Touch::State *s, TouchEvent::Type t, const TouchPoint &p, int64_t now)
{
    TouchEvent e = {t, p.id, p.x, p.y, now};
    if (xQueueSend(s->events, &e, 0) != pdTRUE)
        s->dropped++;
}

static void poll_task(void *arg)
{
    auto *s = static_cast<Touch::State *>(arg);
    TickType_t wake = xTaskGetTickCount();
    const TickType_t period = std::max<TickType_t>(1, pdMS_TO_TICKS(1000 / s->cfg.poll_hz));
    while (!s->stop) {
        vTaskDelayUntil(&wake, period);
        int64_t t0 = esp_timer_get_time();
        uint8_t st = 0;
        esp_err_t e = reg_read(s->dev, 0x814e, &st, 1);
        uint8_t raw[MAX_POINTS * 8];
        int n = -1;
        if (e == ESP_OK && (st & 0x80)) {
            n = st & 0x0f;
            if (n > MAX_POINTS)
                n = MAX_POINTS;
            if (n)
                e = reg_read(s->dev, 0x814f, raw, n * 8);
            if (e == ESP_OK)
                e = reg_write8(s->dev, 0x814e, 0);
        }
        int64_t t1 = esp_timer_get_time();
        s->bus_us += t1 - t0;
        s->polls++;
        if (e != ESP_OK) {
            s->errors++;
            continue;
        }
        if (n < 0)
            continue; /* no new report: the fingers as they were */
        TouchPoint next[MAX_POINTS];
        for (int i = 0; i < n; i++) {
            const uint8_t *p = raw + i * 8;
            next[i].id = p[0];
            to_picture(s->cfg, s->raw_w, s->raw_h, p[1] | p[2] << 8, p[3] | p[4] << 8, next[i].x, next[i].y);
            next[i].size = p[5] | p[6] << 8;
        }
        /* the events: a finger up when its track is gone, down when new, moved when its point changed */
        TouchPoint prev[MAX_POINTS];
        int np;
        portENTER_CRITICAL(&s->lock);
        np = s->count;
        memcpy(prev, s->now, sizeof prev);
        memcpy(s->now, next, sizeof next);
        s->count = n;
        portEXIT_CRITICAL(&s->lock);
        for (int i = 0; i < np; i++) {
            bool kept = false;
            for (int j = 0; j < n; j++)
                kept |= next[j].id == prev[i].id;
            if (!kept)
                push(s, TouchEvent::UP, prev[i], t1);
        }
        for (int j = 0; j < n; j++) {
            int i = 0;
            while (i < np && prev[i].id != next[j].id)
                i++;
            if (i == np)
                push(s, TouchEvent::DOWN, next[j], t1);
            else if (prev[i].x != next[j].x || prev[i].y != next[j].y)
                push(s, TouchEvent::MOVE, next[j], t1);
        }
    }
    xSemaphoreGive(s->stopped);
    vTaskDelete(nullptr);
}

#define TCHECK(x, what)                                                                                              \
    do {                                                                                                             \
        if ((err_ = (x)) != ESP_OK) {                                                                                \
            ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err_));                                                    \
            close();                                                                                                 \
            return false;                                                                                            \
        }                                                                                                            \
    } while (0)

bool Touch::open(const TouchConfig &c)
{
    if (isOpen())
        return true;
    if (c.poll_hz < 1 || c.poll_hz > 1000 ||
        (c.rotation != 0 && c.rotation != 90 && c.rotation != 180 && c.rotation != 270)) {
        err_ = ESP_ERR_INVALID_ARG;
        return false;
    }
    st_ = new State();
    State *s = st_;
    s->cfg = c;
    /* the bus: the one already on the port (the camera's), else a new one (the driver logs an error when it finds
     * none: silenced, it is not one here) */
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
        TCHECK(i2c_new_master_bus(&bc, &s->bus), "the I2C bus");
        s->own_bus = true;
    }
    /* the chip: at the address asked, or at either of its two */
    const int tries[2] = {0x5d, 0x14};
    for (int i = 0; i < 2 && !s->addr; i++) {
        int a = c.address ? c.address : tries[i];
        if (i2c_master_probe(s->bus, a, I2C_TIMEOUT_MS) == ESP_OK)
            s->addr = a;
        if (c.address)
            break;
    }
    if (!s->addr) {
        ESP_LOGE(TAG, "no GT911 at 0x5d or 0x14");
        err_ = ESP_ERR_NOT_FOUND;
        close();
        return false;
    }
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = static_cast<uint16_t>(s->addr);
    dc.scl_speed_hz = c.i2c_hz;
    TCHECK(i2c_master_bus_add_device(s->bus, &dc, &s->dev), "the GT911 on the bus");
    uint8_t id[6], cfg[5];
    TCHECK(reg_read(s->dev, 0x8140, id, 6), "the GT911's product ID");
    TCHECK(reg_read(s->dev, 0x8047, cfg, 5), "the GT911's configuration");
    memcpy(s->product, id, 4);
    s->product[4] = 0;
    s->firmware = id[4] | id[5] << 8;
    s->config_version = cfg[0];
    s->raw_w = cfg[1] | cfg[2] << 8;
    s->raw_h = cfg[3] | cfg[4] << 8;
    TCHECK(reg_write8(s->dev, 0x814e, 0), "the GT911's status");

    s->events = xQueueCreate(QUEUE, sizeof(TouchEvent));
    s->stopped = xSemaphoreCreateBinary();
    if (!s->events || !s->stopped) {
        err_ = ESP_ERR_NO_MEM;
        close();
        return false;
    }
    BaseType_t ok = c.task_core < 0
                        ? xTaskCreate(poll_task, "p4lcd_touch", 3072, s, c.task_priority, &s->task)
                        : xTaskCreatePinnedToCore(poll_task, "p4lcd_touch", 3072, s, c.task_priority, &s->task,
                                                  c.task_core);
    if (ok != pdPASS) {
        s->task = nullptr;
        err_ = ESP_ERR_NO_MEM;
        close();
        return false;
    }
    ESP_LOGI(TAG, "GT%s at 0x%02x: firmware 0x%04x, config version 0x%02x, %d x %d, polled at %d Hz", s->product,
             s->addr, s->firmware, s->config_version, s->raw_w, s->raw_h, c.poll_hz);
    return true;
}

void Touch::close()
{
    State *s = st_;
    if (!s)
        return;
    if (s->task) {
        s->stop = true;
        xSemaphoreTake(s->stopped, portMAX_DELAY);
    }
    if (s->dev)
        i2c_master_bus_rm_device(s->dev);
    if (s->own_bus)
        i2c_del_master_bus(s->bus); /* refused (and kept) while another device is still on it */
    if (s->events)
        vQueueDelete(s->events);
    if (s->stopped)
        vSemaphoreDelete(s->stopped);
    delete s;
    st_ = nullptr;
}

int Touch::points(TouchPoint *out, int max) const
{
    if (!st_)
        return 0;
    portENTER_CRITICAL(&st_->lock);
    int n = std::min(st_->count, max);
    memcpy(out, st_->now, n * sizeof(TouchPoint));
    portEXIT_CRITICAL(&st_->lock);
    return n;
}

bool Touch::event(TouchEvent &e, uint32_t timeout_ms)
{
    return st_ && xQueueReceive(st_->events, &e, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

const char *Touch::product() const { return st_ ? st_->product : ""; }
int Touch::firmware() const { return st_ ? st_->firmware : 0; }
int Touch::configVersion() const { return st_ ? st_->config_version : 0; }
int Touch::rawWidth() const { return st_ ? st_->raw_w : 0; }
int Touch::rawHeight() const { return st_ ? st_->raw_h : 0; }
int Touch::address() const { return st_ ? st_->addr : 0; }
uint32_t Touch::polls() const { return st_ ? st_->polls.load() : 0; }
uint32_t Touch::busErrors() const { return st_ ? st_->errors.load() : 0; }
uint32_t Touch::droppedEvents() const { return st_ ? st_->dropped.load() : 0; }

float Touch::busMsPerPoll() const
{
    if (!st_ || !st_->polls.load())
        return 0;
    return st_->bus_us.load() / 1000.0f / st_->polls.load();
}

} // namespace p4lcd
