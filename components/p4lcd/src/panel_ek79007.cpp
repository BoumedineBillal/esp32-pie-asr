// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0

/*
 * The EK79007 1024 x 600 MIPI-DSI panel of the ESP32-P4X-Function-EV-Board.
 *
 * The init commands, the reset timing and the video timing are copied from Espressif's esp_lcd_ek79007 2.0.2
 * (esp_lcd_ek79007.c: the pad control command and vendor_specific_init_default; include/esp_lcd_ek79007.h:
 * EK79007_1024_600_PANEL_60HZ_CONFIG_CF), which is:
 *
 *   SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *   SPDX-License-Identifier: Apache-2.0
 *
 * and from Espressif's board package for the lane count and rate (2 lanes, 1000 Mbps).
 */
#include "panel.hpp"

namespace p4lcd::detail {

static const PanelCmd EK79007_INIT[] = {
    {0xB2, 1, {0x10}, 0},  /* pad control: 2 data lanes (0x00 would be 4) */
    {0x80, 1, {0x8B}, 0},  /* vendor registers, as the panel supplier gives them */
    {0x81, 1, {0x78}, 0},
    {0x82, 1, {0x84}, 0},
    {0x83, 1, {0x88}, 0},
    {0x84, 1, {0xA8}, 0},
    {0x85, 1, {0xE3}, 0},
    {0x86, 1, {0x88}, 0},
    {0x11, 0, {}, 120},    /* sleep out, then 120 ms */
};

const Panel EK79007_1024X600 = {
    "EK79007 1024x600",
    1024, 600,
    2, 1000,
    10, 160, 160, 1, 23, 12,
    52.0f,                 /* 52 MHz over 1354 x 636 = 60.4 Hz */
    10, 20,
    EK79007_INIT, sizeof(EK79007_INIT) / sizeof(EK79007_INIT[0]),
};

} // namespace p4lcd::detail
