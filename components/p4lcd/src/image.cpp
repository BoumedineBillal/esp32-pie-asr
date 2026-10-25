// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * Canvas::draw (p4lcd.hpp): an image into the picture being drawn, by the PPA's scale-rotate-mirror engine.
 *
 * The PPA writes straight into the frame buffer. Before it does, the driver invalidates the cache over the rows it
 * writes (whole rows), which would throw away CPU drawing still in the cache there: so those rows are written back
 * first. An image partly off the screen is cut to the source pixels whose scaled footprint is inside (exact at scale
 * 1; at other scales the cut edge may differ by a pixel from the uncut image).
 */
#include <algorithm>
#include <cmath>
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_log.h"
#include "p4lcd.hpp"

namespace p4lcd {

static const char *TAG = "p4lcd";

bool Canvas::draw(const Image &img, int x, int y, float scale, int rotation)
{
    if (!buf_) {
        err_ = ESP_ERR_INVALID_STATE;
        return false;
    }
    /* the PPA's driver invalidates the output's rows to whole cache lines: a buffer that shares a line with other
     * data would lose that data's unwritten changes (a created canvas's allocation is rounded up: its end is safe) */
    const size_t line = esp_cache_get_line_size_by_addr(buf_);
    if (line && (reinterpret_cast<uintptr_t>(buf_) % line || (!owned_ && (size_t)stride_ * ph_ * 2 % line))) {
        ESP_LOGE(TAG, "draw: the canvas's buffer must start and end on a cache line (%u bytes)", (unsigned)line);
        err_ = ESP_ERR_INVALID_ARG;
        return false;
    }
    const bool yuv = img.format == PixelFormat::YUV420;
    const int stride = img.stride ? img.stride : img.width;
    const int q = static_cast<int>(scale * 16.0f + 1e-3f); /* the scale in 1/16 steps, rounded down */
    if (!img.data || img.width <= 0 || img.height <= 0 || stride < img.width || rotation % 90 || rotation < 0 ||
        rotation >= 360 || q < 1 || q >= 256 || (yuv && (img.width % 2 || img.height % 2 || stride % 2))) {
        ESP_LOGE(TAG, "draw: an empty image, a rotation other than 0 / 90 / 180 / 270, a scale outside 1/16 - 16, or "
                      "a YUV420 image of odd size");
        err_ = ESP_ERR_INVALID_ARG;
        return false;
    }
    /* the PPA's own rounding: n pixels scaled by q / 16 */
    auto scaled = [q](int n) { return n * (q / 16) + n * (q % 16) / 16; };
    const int sw = scaled(img.width), sh = scaled(img.height);
    if (!sw || !sh)
        return true;
    const int t = (rotation_ + rotation) % 360; /* clockwise, against the panel */

    /* the drawn rectangle in picture coordinates, then in the panel's layout (as fillRect, before clipping) */
    const int dw = rotation % 180 ? sh : sw, dh = rotation % 180 ? sw : sh;
    int px, py, pw, ph;
    switch (rotation_) {
    case 90: px = pw_ - y - dh; py = x; pw = dh; ph = dw; break;
    case 180: px = pw_ - x - dw; py = ph_ - y - dh; pw = dw; ph = dh; break;
    case 270: px = y; py = ph_ - x - dw; pw = dh; ph = dw; break;
    default: px = x; py = y; pw = dw; ph = dh; break;
    }
    const int vx0 = std::max(px, 0), vy0 = std::max(py, 0);
    const int vx1 = std::min(px + pw, pw_), vy1 = std::min(py + ph, ph_);
    if (vx0 >= vx1 || vy0 >= vy1)
        return true; /* nothing on screen */

    int bx = 0, by = 0, bw = img.width, bh = img.height, ox = px, oy = py;
    if (vx0 != px || vy0 != py || vx1 != px + pw || vy1 != py + ph) {
        /* the visible part, relative to the output's corner, into source-scaled coordinates (u along the source's
         * rows, v along its columns) */
        auto inv = [&](int ox_, int oy_, int &u, int &v) {
            switch (t) {
            case 90: u = oy_; v = sh - 1 - ox_; break;
            case 180: u = sw - 1 - ox_; v = sh - 1 - oy_; break;
            case 270: u = sw - 1 - oy_; v = ox_; break;
            default: u = ox_; v = oy_; break;
            }
        };
        int u0, v0, u1, v1;
        inv(vx0 - px, vy0 - py, u0, v0);
        inv(vx1 - 1 - px, vy1 - 1 - py, u1, v1);
        if (u0 > u1)
            std::swap(u0, u1);
        if (v0 > v1)
            std::swap(v0, v1);
        /* the source pixels whose scaled footprint lies inside: [ceil(u0 / s), floor((u1 + 1) / s)) */
        const float s = q / 16.0f;
        int sx0 = std::max(0, static_cast<int>(std::ceil(u0 / s - 1e-4f)));
        int sy0 = std::max(0, static_cast<int>(std::ceil(v0 / s - 1e-4f)));
        int sx1 = std::min(img.width, static_cast<int>(std::floor((u1 + 1) / s + 1e-4f)));
        int sy1 = std::min(img.height, static_cast<int>(std::floor((v1 + 1) / s + 1e-4f)));
        if (yuv) { /* even offsets and sizes */
            sx0 += sx0 & 1;
            sy0 += sy0 & 1;
            sx1 -= (sx1 - sx0) & 1;
            sy1 -= (sy1 - sy0) & 1;
        }
        bx = sx0;
        by = sy0;
        bw = sx1 - sx0;
        bh = sy1 - sy0;
        if (bw <= 0 || bh <= 0)
            return true;
        /* where that block lands: its scaled rectangle, turned into the output */
        const int us = scaled(bx), vs = scaled(by), bws = scaled(bw), bhs = scaled(bh);
        switch (t) {
        case 90: ox = sh - vs - bhs; oy = us; break;
        case 180: ox = sw - us - bws; oy = sh - vs - bhs; break;
        case 270: ox = vs; oy = sw - us - bws; break;
        default: ox = us; oy = vs; break;
        }
        ox += px;
        oy += py;
    }
    const int ow = t % 180 ? scaled(bh) : scaled(bw), oh = t % 180 ? scaled(bw) : scaled(bh);
    if (ox < 0 || oy < 0 || ox + ow > pw_ || oy + oh > ph_) { /* cannot happen by construction; refuse rather than
                                                                * let the PPA write outside */
        ESP_LOGE(TAG, "draw: the block %dx%d at %d,%d falls outside the panel", ow, oh, ox, oy);
        err_ = ESP_ERR_INVALID_STATE;
        return false;
    }

    if (!ppa_) {
        ppa_client_config_t pc = {};
        pc.oper_type = PPA_OPERATION_SRM;
        ppa_client_handle_t h = nullptr;
        if ((err_ = ppa_register_client(&pc, &h)) != ESP_OK) {
            ESP_LOGE(TAG, "draw: the PPA: %s", esp_err_to_name(err_));
            return false;
        }
        ppa_ = h;
    }
    /* the CPU's drawing in the rows the PPA writes: to memory first (the driver invalidates those rows) */
    esp_cache_msync(buf_ + (size_t)oy * stride_, (size_t)oh * stride_ * sizeof(Color),
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

    ppa_srm_oper_config_t op = {};
    op.in.buffer = img.data;
    op.in.pic_w = stride;
    op.in.pic_h = img.height;
    op.in.block_w = bw;
    op.in.block_h = bh;
    op.in.block_offset_x = bx;
    op.in.block_offset_y = by;
    op.in.srm_cm = img.format == PixelFormat::RGB565 ? PPA_SRM_COLOR_MODE_RGB565
                   : yuv                             ? PPA_SRM_COLOR_MODE_YUV420
                                                     : PPA_SRM_COLOR_MODE_RGB888;
    op.in.yuv_range = PPA_COLOR_RANGE_FULL;
    op.in.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;
    op.out.buffer = buf_;
    op.out.buffer_size = (uint32_t)stride_ * ph_ * sizeof(Color);
    op.out.pic_w = stride_;
    op.out.pic_h = ph_;
    op.out.block_offset_x = ox;
    op.out.block_offset_y = oy;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    /* the PPA turns counter-clockwise */
    static const ppa_srm_rotation_angle_t ccw[4] = {PPA_SRM_ROTATION_ANGLE_0, PPA_SRM_ROTATION_ANGLE_270,
                                                    PPA_SRM_ROTATION_ANGLE_180, PPA_SRM_ROTATION_ANGLE_90};
    op.rotation_angle = ccw[t / 90];
    op.scale_x = op.scale_y = q / 16.0f;
    op.rgb_swap = img.format == PixelFormat::RGB888; /* the PPA reads B, G, R */
    op.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    op.mode = PPA_TRANS_MODE_BLOCKING;
    if ((err_ = ppa_do_scale_rotate_mirror(static_cast<ppa_client_handle_t>(ppa_), &op)) != ESP_OK) {
        ESP_LOGE(TAG, "draw: the PPA: %s", esp_err_to_name(err_));
        return false;
    }
    return true;
}

} // namespace p4lcd
