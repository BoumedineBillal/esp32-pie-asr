# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

set(srcs "app_main.cpp" "settings.cpp" "ui/ui.cpp" "ui/screen_live.cpp" "ui/screen_stats.cpp" "ui/screen_history.cpp"
         "ui/screen_phrases.cpp" "ui/screen_settings.cpp" "ui/screen_about.cpp")
# the debug link is linked only when its commands are built in (pclink is listed either way: a component's
# requirements cannot depend on the configuration)
if(CONFIG_LIVE_TRANSCRIBER_DEBUG_LINK)
    list(APPEND srcs "../debug/commands.cpp")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "../phrases/default.txt"
                       PRIV_REQUIRES asr_service p4lcd pclink esp_timer heap nvs_flash)
