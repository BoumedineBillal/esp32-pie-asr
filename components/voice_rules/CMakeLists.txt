# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

# words to actions by rules (no ESP-IDF dependency: host/ builds the same files and their tests)
idf_component_register(SRCS "voice_rules.cpp" "demos.cpp" "vacuum_rules.cpp" "vacuum_sim.cpp" "smarthome_rules.cpp"
                            "car_rules.cpp" "factory_rules.cpp" "kitchen_rules.cpp" "presenter_rules.cpp"
                            "smarthome_sim.cpp" "car_sim.cpp" "factory_sim.cpp" "kitchen_sim.cpp" "presenter_sim.cpp"
                       INCLUDE_DIRS "include")
