# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

# p4lcd: the LCD of the ESP32-P4X-Function-EV-Board
idf_component_register(SRCS "src/canvas.cpp" "src/display.cpp" "src/draw.cpp" "src/font_8x16.cpp" "src/image.cpp"
                            "src/panel_ek79007.cpp" "src/touch.cpp" "src/ttf.cpp"
                            "src/ui_paint.cpp" "src/ui_widgets.cpp" "src/ui_input.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src" "third_party"
                       EMBED_FILES "fonts/inter_regular.ttf" "fonts/inter_bold.ttf"
                       PRIV_REQUIRES esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_ppa
                                     esp_hw_support esp_mm esp_rom esp_timer)
