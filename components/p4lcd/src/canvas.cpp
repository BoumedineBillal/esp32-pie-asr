// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Canvas (p4lcd.hpp): a picture to draw into, of its own (PSRAM) or a buffer of the caller's. The drawing calls are
 * in draw.cpp, image.cpp and ttf.cpp; the Display is a Canvas whose buffer is the panel's frame being drawn.
 */
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "p4lcd.hpp"

namespace p4lcd {

static const char *TAG = "p4lcd";
static constexpr size_t ALIGN = 128; /* the largest L2 cache line of the P4 (CONFIG_CACHE_L2_CACHE_LINE_SIZE) */

bool Canvas::create(int width, int height, Color c)
{
    release();
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        err_ = ESP_ERR_INVALID_ARG;
        return false;
    }
    size_t bytes = ((size_t)width * height * sizeof(Color) + ALIGN - 1) / ALIGN * ALIGN;
    buf_ = static_cast<Color *>(heap_caps_aligned_alloc(ALIGN, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf_) {
        ESP_LOGE(TAG, "a %dx%d canvas: no PSRAM for %u bytes", width, height, (unsigned)bytes);
        err_ = ESP_ERR_NO_MEM;
        return false;
    }
    owned_ = true;
    pw_ = stride_ = width;
    ph_ = height;
    rotation_ = 0;
    err_ = ESP_OK;
    fill(c);
    return true;
}

bool Canvas::wrap(Color *pixels, int width, int height, int stride)
{
    release();
    if (!pixels || width <= 0 || height <= 0 || (stride && stride < width)) {
        err_ = ESP_ERR_INVALID_ARG;
        return false;
    }
    buf_ = pixels;
    owned_ = false;
    pw_ = width;
    stride_ = stride ? stride : width;
    ph_ = height;
    rotation_ = 0;
    err_ = ESP_OK;
    return true;
}

void Canvas::detach()
{
    if (ppa_)
        ppa_unregister_client(static_cast<ppa_client_handle_t>(ppa_));
    ppa_ = nullptr;
    if (owned_)
        heap_caps_free(buf_);
    buf_ = nullptr;
    owned_ = false;
    pw_ = ph_ = stride_ = 0;
}

void Canvas::release() { detach(); }

Image Canvas::image() const
{
    Image img;
    img.data = buf_;
    img.width = pw_;
    img.height = ph_;
    img.stride = stride_;
    img.format = PixelFormat::RGB565;
    return img;
}

} // namespace p4lcd
