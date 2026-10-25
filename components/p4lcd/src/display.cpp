// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Display (p4lcd.hpp): the MIPI-DSI bus, the panel's init commands, its DPI video mode with two frame buffers, the
 * backlight. The panel is described in panel_ek79007.cpp.
 */
#include <atomic>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "p4lcd.hpp"
#include "panel.hpp"

namespace p4lcd {

static const char *TAG = "p4lcd";
static constexpr ledc_timer_t BL_TIMER = LEDC_TIMER_3;       /* the backlight's PWM: 5 kHz, 10 bits */
static constexpr ledc_channel_t BL_CHANNEL = LEDC_CHANNEL_7;

struct Display::State {
    std::atomic<uint32_t> vsyncs{0};
    std::atomic<int64_t> first_us{0}, last_us{0};
    SemaphoreHandle_t frame_done = nullptr;   /* given at each frame's end: the buffer before it is free */
    StaticSemaphore_t frame_done_mem;
};

/* the panel's interrupt callbacks */
static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *ctx)
{
    auto *st = static_cast<Display::State *>(ctx);
    int64_t t = esp_timer_get_time();
    if (st->vsyncs.fetch_add(1) == 0)
        st->first_us = t;
    st->last_us = t;
    return false;
}

static bool IRAM_ATTR on_frame_done(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *ctx)
{
    auto *st = static_cast<Display::State *>(ctx);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(st->frame_done, &woken);
    return woken == pdTRUE;
}

#define CHECK(x, what)                                                                                               \
    do {                                                                                                             \
        if ((err_ = (x)) != ESP_OK) {                                                                                \
            ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err_));                                                    \
            close();                                                                                                 \
            return false;                                                                                            \
        }                                                                                                            \
    } while (0)

bool Display::open(const Config &c)
{
    if (isOpen())
        return true;
    const detail::Panel &p = detail::EK79007_1024X600;
    if (c.rotation != 0 && c.rotation != 90 && c.rotation != 180 && c.rotation != 270) {
        ESP_LOGE(TAG, "rotation %d: 0, 90, 180 or 270", c.rotation);
        err_ = ESP_ERR_INVALID_ARG;
        return false;
    }
    if (c.frame_buffers != 1 && c.frame_buffers != 2) {
        ESP_LOGE(TAG, "frame_buffers %d: 1 or 2", c.frame_buffers);
        err_ = ESP_ERR_INVALID_ARG;
        return false;
    }
    release(); /* a Canvas that was created or wrapped before: let go */
    rotation_ = c.rotation;
    pw_ = stride_ = p.width;
    ph_ = p.height;
    st_ = new State();
    st_->frame_done = xSemaphoreCreateBinaryStatic(&st_->frame_done_mem);

    /* the DSI PHY's supply */
    if (c.dsi_ldo_channel >= 0) {
        esp_ldo_channel_config_t ldo = {};
        ldo.chan_id = c.dsi_ldo_channel;
        ldo.voltage_mv = c.dsi_ldo_mv;
        esp_ldo_channel_handle_t h = nullptr;
        CHECK(esp_ldo_acquire_channel(&ldo, &h), "the DSI PHY's LDO");
        ldo_ = h;
    }

    /* the bus, and the command interface the init commands go through */
    esp_lcd_dsi_bus_config_t bus = {};
    bus.bus_id = 0;
    bus.num_data_lanes = p.lanes;
    bus.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;
    bus.lane_bit_rate_mbps = p.lane_mbps;
    esp_lcd_dsi_bus_handle_t bus_h = nullptr;
    CHECK(esp_lcd_new_dsi_bus(&bus, &bus_h), "the DSI bus");
    bus_ = bus_h;
    esp_lcd_dbi_io_config_t dbi = {};
    dbi.virtual_channel = 0;
    dbi.lcd_cmd_bits = 8;
    dbi.lcd_param_bits = 8;
    esp_lcd_panel_io_handle_t io_h = nullptr;
    CHECK(esp_lcd_new_panel_io_dbi(bus_h, &dbi, &io_h), "the DSI command interface");
    io_ = io_h;

    /* the panel: reset, then its init commands */
    if (c.reset_pin >= 0) {
        reset_pin_ = c.reset_pin;
        gpio_config_t g = {};
        g.pin_bit_mask = 1ULL << c.reset_pin;
        g.mode = GPIO_MODE_OUTPUT;
        CHECK(gpio_config(&g), "the panel's reset pin");
        gpio_set_level(static_cast<gpio_num_t>(c.reset_pin), 0);
        vTaskDelay(pdMS_TO_TICKS(p.reset_low_ms));
        gpio_set_level(static_cast<gpio_num_t>(c.reset_pin), 1);
        vTaskDelay(pdMS_TO_TICKS(p.reset_wait_ms));
    }
    for (int i = 0; i < p.ninit; i++) {
        const detail::PanelCmd &cmd = p.init[i];
        CHECK(esp_lcd_panel_io_tx_param(io_h, cmd.cmd, cmd.len ? cmd.data : nullptr, cmd.len), "a panel command");
        if (cmd.wait_ms)
            vTaskDelay(pdMS_TO_TICKS(cmd.wait_ms));
    }

    /* the video mode: one or two RGB565 frame buffers, the pixel clock from the refresh rate asked for */
    uint32_t htotal = p.width + p.hsync + p.hbp + p.hfp, vtotal = p.height + p.vsync + p.vbp + p.vfp;
    pclk_mhz_ = c.refresh_hz > 0 ? c.refresh_hz * (float)htotal * vtotal / 1e6f : p.pclk_mhz;
    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.virtual_channel = 0;
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi.dpi_clock_freq_mhz = pclk_mhz_;
    dpi.in_color_format = LCD_COLOR_FMT_RGB565;
    dpi.num_fbs = c.frame_buffers;
    dpi.video_timing.h_size = p.width;
    dpi.video_timing.v_size = p.height;
    dpi.video_timing.hsync_pulse_width = p.hsync;
    dpi.video_timing.hsync_back_porch = p.hbp;
    dpi.video_timing.hsync_front_porch = p.hfp;
    dpi.video_timing.vsync_pulse_width = p.vsync;
    dpi.video_timing.vsync_back_porch = p.vbp;
    dpi.video_timing.vsync_front_porch = p.vfp;
    esp_lcd_panel_handle_t panel_h = nullptr;
    CHECK(esp_lcd_new_panel_dpi(bus_h, &dpi, &panel_h), "the DPI panel");
    panel_ = panel_h;
    esp_lcd_dpi_panel_event_callbacks_t cbs = {};
    cbs.on_frame_buf_complete = on_frame_done;
    cbs.on_vsync = on_vsync;
    CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel_h, &cbs, st_), "the panel's callbacks");
    CHECK(esp_lcd_panel_init(panel_h), "the video mode");
    void *fb0 = nullptr, *fb1 = nullptr;
    if (c.frame_buffers == 2) {
        CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel_h, 2, &fb0, &fb1), "the frame buffers");
    } else {
        CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel_h, 1, &fb0), "the frame buffer");
        fb1 = fb0; /* one buffer: drawn where it is shown */
    }
    front_ = static_cast<Color *>(fb0);   /* the driver shows the first one; both start black */
    buf_ = static_cast<Color *>(fb1);

    /* the backlight */
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.timer_num = BL_TIMER;
    t.freq_hz = 5000;
    t.clk_cfg = LEDC_AUTO_CLK;
    CHECK(ledc_timer_config(&t), "the backlight's timer");
    ledc_channel_config_t ch = {};
    ch.gpio_num = c.backlight_pin;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = BL_CHANNEL;
    ch.timer_sel = BL_TIMER;
    ch.duty = 0;
    CHECK(ledc_channel_config(&ch), "the backlight's channel");
    backlight_pin_ = c.backlight_pin;
    if (!brightness(c.brightness)) {
        close();
        return false;
    }
    ESP_LOGI(TAG, "%s: %dx%d, %d lanes x %d Mbps, pixel clock %.2f MHz asked", p.name, pw_, ph_, p.lanes,
             p.lane_mbps, pclk_mhz_);
    return true;
}

void Display::close()
{
    if (backlight_pin_ >= 0) {
        ledc_stop(LEDC_LOW_SPEED_MODE, BL_CHANNEL, 0);
        gpio_reset_pin(static_cast<gpio_num_t>(backlight_pin_));
        backlight_pin_ = -1;
    }
    detach(); /* the frame buffers are the driver's */
    if (panel_)
        esp_lcd_panel_del(static_cast<esp_lcd_panel_handle_t>(panel_));
    if (io_)
        esp_lcd_panel_io_del(static_cast<esp_lcd_panel_io_handle_t>(io_));
    if (bus_)
        esp_lcd_del_dsi_bus(static_cast<esp_lcd_dsi_bus_handle_t>(bus_));
    if (ldo_)
        esp_ldo_release_channel(static_cast<esp_ldo_channel_handle_t>(ldo_));
    if (reset_pin_ >= 0)
        gpio_reset_pin(static_cast<gpio_num_t>(reset_pin_));
    panel_ = io_ = bus_ = ldo_ = nullptr;
    reset_pin_ = -1;
    front_ = nullptr;
    delete st_;
    st_ = nullptr;
}

bool Display::present(uint32_t timeout_ms)
{
    if (!isOpen()) {
        err_ = ESP_ERR_INVALID_STATE;
        return false;
    }
    xSemaphoreTake(st_->frame_done, 0); /* forget a frame end from before */
    /* the drawn buffer is one of the frame buffers: the driver writes it back from the cache and scans it out from
     * the next frame on (no copy) */
    err_ = esp_lcd_panel_draw_bitmap(static_cast<esp_lcd_panel_handle_t>(panel_), 0, 0, pw_, ph_, buf_);
    if (err_ != ESP_OK)
        return false;
    /* the end of the frame being scanned now: from there on the panel reads the drawn buffer, and the old one is
     * free */
    if (xSemaphoreTake(st_->frame_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        err_ = ESP_ERR_TIMEOUT;
        return false;
    }
    if (buf_ == front_) /* one frame buffer: nothing to swap */
        return true;
    Color *shown = buf_;
    buf_ = front_;
    front_ = shown;
    return true;
}

bool Display::brightness(int percent)
{
    if (backlight_pin_ < 0) {
        err_ = ESP_ERR_INVALID_STATE;
        return false;
    }
    percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    uint32_t duty = (1023u * percent) / 100;
    if ((err_ = ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty)) == ESP_OK)
        err_ = ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
    return err_ == ESP_OK;
}

uint32_t Display::refreshes() const { return st_ ? st_->vsyncs.load() : 0; }

float Display::refreshHz() const
{
    if (!st_ || st_->vsyncs.load() < 2)
        return 0;
    return (st_->vsyncs.load() - 1) * 1e6f / (float)(st_->last_us.load() - st_->first_us.load());
}

} // namespace p4lcd
