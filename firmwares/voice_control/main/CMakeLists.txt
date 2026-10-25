# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

set(srcs "app_main.cpp" "settings.cpp" "demo.cpp" "ui/ui.cpp" "ui/icons.cpp" "ui/screen_home.cpp"
         "ui/screen_demo.cpp" "ui/demo_pages.cpp" "ui/env_vacuum.cpp" "ui/env_smarthome.cpp" "ui/env_car.cpp"
         "ui/env_factory.cpp" "ui/env_kitchen.cpp" "ui/env_presenter.cpp" "ui/screen_live.cpp"
         "ui/screen_stats.cpp" "ui/screen_history.cpp" "ui/screen_phrases.cpp" "ui/screen_settings.cpp"
         "ui/screen_about.cpp")
# the debug link is linked only when its commands are built in (pclink is listed either way: a component's
# requirements cannot depend on the configuration)
if(CONFIG_VOICE_CONTROL_DEBUG_LINK)
    list(APPEND srcs "../debug/commands.cpp")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "../phrases/default.txt" "../phrases/vacuum.txt" "../phrases/smarthome.txt"
                                      "../phrases/car.txt" "../phrases/factory.txt" "../phrases/kitchen.txt"
                                      "../phrases/presenter.txt"
                       PRIV_REQUIRES asr_service p4lcd pclink voice_rules esp_timer heap nvs_flash)
