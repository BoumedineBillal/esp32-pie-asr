# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

# p4sdcard: the microSD card of the ESP32-P4X-Function-EV-Board
idf_component_register(SRCS "src/card.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES fatfs sdmmc esp_driver_sdmmc vfs esp_rom heap)
