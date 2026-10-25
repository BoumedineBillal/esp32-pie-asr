# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS "service.cpp" "audio_ring.cpp" "stream.cpp" "model_loader.cpp" "transcript.cpp"
                            "rewrite.cpp" "phrases.cpp" "transcript_save.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES pieasr
                       PRIV_REQUIRES p4mic p4sdcard esp_timer esp_rom heap)
