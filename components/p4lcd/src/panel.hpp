// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The panel: what the display layer needs to know to run it. One table per panel model; the board's panel is the
 * EK79007 1024 x 600 module of the ESP32-P4X-Function-EV-Board.
 */
#pragma once

#include <cstdint>

namespace p4lcd::detail {

struct PanelCmd {
    uint8_t cmd;
    uint8_t len;      /* parameter bytes */
    uint8_t data[4];
    uint16_t wait_ms; /* after the command */
};

struct Panel {
    const char *name;
    uint16_t width, height;
    uint8_t lanes;
    uint16_t lane_mbps;
    /* the video timing: the visible size plus sync, back porch and front porch, in pixels / lines */
    uint16_t hsync, hbp, hfp, vsync, vbp, vfp;
    float pclk_mhz;   /* the vendor's pixel clock (its refresh rate follows from the timing) */
    uint16_t reset_low_ms, reset_wait_ms; /* the reset pin low for, then the wait before the first command */
    const PanelCmd *init;
    uint8_t ninit;
};

extern const Panel EK79007_1024X600;

} // namespace p4lcd::detail
