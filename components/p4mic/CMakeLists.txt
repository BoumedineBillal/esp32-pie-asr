# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

# p4mic: the microphone of the ESP32-P4X-Function-EV-Board
idf_component_register(SRCS "src/es8311.cpp" "src/microphone.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       PRIV_REQUIRES esp_driver_i2s esp_driver_i2c esp_timer)
