# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS "pclink.cpp" "asr_commands.cpp" "model_over_usb.cpp" "audio_feed.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES asr_service
                       PRIV_REQUIRES usb_stream p4sdcard esp_timer esp_system heap freertos)
