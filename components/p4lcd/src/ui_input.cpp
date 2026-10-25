// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * p4lcd_ui.hpp: the scroll view, the touch input, the screen dump.
 */
#include <cstdio>
#include <cstring>
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "p4lcd_ui.hpp"

namespace p4lcd::ui {

/* ---- the scroll view ---- */

void Scroll::update(int content_height)
{
    content_h = content_height;
    const int m = maxOffset();
    if (follow)
        offset = m;
    offset = offset < 0 ? 0 : offset > m ? m : offset;
}

void Scroll::drag(int dy)
{
    follow = false;
    offset = grab_offset - dy;
}

void Scroll::release()
{
    if (offset >= maxOffset() - 4)
        follow = true;
}

void Scroll::edges(Painter &p, Color bg, int fade) const
{
    const int vh = viewHeight();
    if (offset > 0 && p.visible({view.x, view.y, view.w, fade}))
        p.fade(view.x, view.y, view.w, fade, bg, true);
    if (offset < maxOffset() && p.visible({view.x, view.y + vh - fade, view.w, fade}))
        p.fade(view.x, view.y + vh - fade, view.w, fade, bg, false);
}

void Scroll::scrollbar(Painter &p, int x, Color c) const
{
    const int vh = viewHeight();
    if (content_h <= vh)
        return;
    const int th = vh * vh / content_h > 30 ? vh * vh / content_h : 30, m = maxOffset();
    const int ty = view.y + (vh - th) * (offset < m ? offset : m) / (m > 1 ? m : 1);
    p.round(x + 2, ty, 6, th, 3, c);
}

void Scroll::backBand(Painter &p, const Theme &t, const Button &b, const char *hint, const char *id) const
{
    if (!showsBack())
        return;
    const int by = view.y + view.h - back_band;
    p.rect(view.x, by, view.w, back_band, t.panel);
    p.rect(view.x, by, view.w - 14, 1, t.line);
    p.fit({view.x, by + 8, view.w - 230, 40}, hint, style(18, t.muted, false, Align::LEFT, VAlign::MIDDLE), id);
    button(p, t, b, id);
}

/* ---- touch input ---- */

bool Input::open(Touch *touch)
{
    touch_ = touch;
    if (!queue_)
        queue_ = xQueueCreate(32, sizeof(TouchEvent));
    return queue_ != nullptr;
}

void Input::inject(TouchEvent::Type type, int x, int y)
{
    if (!queue_)
        return;
    TouchEvent e = {type, 0, x, y, esp_timer_get_time()};
    xQueueSend(static_cast<QueueHandle_t>(queue_), &e, pdMS_TO_TICKS(100));
}

void Input::tap(int x, int y, uint32_t hold_ms)
{
    inject(TouchEvent::DOWN, x, y);
    vTaskDelay(pdMS_TO_TICKS(hold_ms));
    inject(TouchEvent::UP, x, y);
}

void Input::drag(int x0, int y0, int x1, int y1, int steps, uint32_t step_ms)
{
    inject(TouchEvent::DOWN, x0, y0);
    for (int k = 1; k <= steps; k++) {
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        inject(TouchEvent::MOVE, x0 + (x1 - x0) * k / steps, y0 + (y1 - y0) * k / steps);
    }
    inject(TouchEvent::UP, x1, y1);
}

bool Input::next(TouchEvent &e)
{
    if (touch_ && touch_->isOpen() && touch_->event(e, 0))
        return true;
    return queue_ && xQueueReceive(static_cast<QueueHandle_t>(queue_), &e, 0);
}

/* ---- the screen dump ---- */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* base64 lines of 96 characters (72 bytes), the last one padded */
struct B64Out {
    uint8_t in[72];
    int n = 0;
    size_t total = 0;
    void put(uint8_t b)
    {
        in[n++] = b;
        total++;
        if (n == sizeof in)
            flush();
    }
    void flush()
    {
        char out[100];
        int o = 0;
        for (int i = 0; i < n; i += 3) {
            const uint32_t v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
            out[o++] = B64[v >> 18 & 63];
            out[o++] = B64[v >> 12 & 63];
            out[o++] = i + 1 < n ? B64[v >> 6 & 63] : '=';
            out[o++] = i + 2 < n ? B64[v & 63] : '=';
        }
        out[o] = 0;
        if (o)
            printf("%s\n", out);
        n = 0;
    }
};

bool printScreen(const Display &d, const char *name)
{
    const Color *px = d.screen();
    if (!px)
        return false;
    const int rot = d.rotation(), pw = rot % 180 ? d.height() : d.width(), ph = rot % 180 ? d.width() : d.height();
    const size_t stride = d.stride();
    /* the code's size and the pixels' CRC first (two passes: the first counts the runs) */
    size_t runs = 0;
    uint32_t crc = 0;
    for (int y = 0; y < ph; y++) {
        const Color *row = px + static_cast<size_t>(y) * stride;
        crc = esp_rom_crc32_le(crc, reinterpret_cast<const uint8_t *>(row), sizeof(Color) * pw);
        for (int x = 0; x < pw;) {
            int n = 1;
            while (x + n < pw && n < 65535 && row[x + n] == row[x])
                n++;
            runs++;
            x += n;
        }
    }
    printf("P4LCD-SCREEN %s %d %d %d %u %08lx\n", name ? name : "screen", pw, ph, rot, (unsigned)(runs * 4),
           (unsigned long)crc);
    B64Out out;
    for (int y = 0; y < ph; y++) {
        const Color *row = px + static_cast<size_t>(y) * stride;
        for (int x = 0; x < pw;) {
            int n = 1;
            while (x + n < pw && n < 65535 && row[x + n] == row[x])
                n++;
            out.put(n & 255);
            out.put(n >> 8);
            out.put(row[x] & 255);
            out.put(row[x] >> 8);
            x += n;
        }
    }
    out.flush();
    printf("P4LCD-SCREEN-END\n");
    fflush(stdout);
    return true;
}

} // namespace p4lcd::ui
