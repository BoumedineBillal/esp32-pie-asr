# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

# The rules on the PC: their tests, and voice_rules_cli (transcribe's output -> the actions).
#   cmake -S components/voice_rules/host -B build/voice_rules && cmake --build build/voice_rules
#   ctest --test-dir build/voice_rules
cmake_minimum_required(VERSION 3.16)
project(voice_rules_host CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)

set(SRC ${CMAKE_CURRENT_LIST_DIR}/..)
add_library(voice_rules STATIC ${SRC}/voice_rules.cpp ${SRC}/demos.cpp ${SRC}/vacuum_rules.cpp ${SRC}/vacuum_sim.cpp
                               ${SRC}/smarthome_rules.cpp ${SRC}/car_rules.cpp ${SRC}/factory_rules.cpp
                               ${SRC}/kitchen_rules.cpp ${SRC}/presenter_rules.cpp ${SRC}/smarthome_sim.cpp
                               ${SRC}/car_sim.cpp ${SRC}/factory_sim.cpp ${SRC}/kitchen_sim.cpp
                               ${SRC}/presenter_sim.cpp)
target_include_directories(voice_rules PUBLIC ${SRC}/include)

add_executable(test_vacuum_rules test_vacuum_rules.cpp)
target_link_libraries(test_vacuum_rules voice_rules)
add_executable(test_vacuum_sim test_vacuum_sim.cpp)
target_link_libraries(test_vacuum_sim voice_rules)
add_executable(test_demo_rules test_demo_rules.cpp)
target_link_libraries(test_demo_rules voice_rules)
add_executable(test_demo_sims test_demo_sims.cpp)
target_link_libraries(test_demo_sims voice_rules)
add_executable(voice_rules_cli voice_rules_cli.cpp)
target_link_libraries(voice_rules_cli voice_rules)

enable_testing()
add_test(NAME vacuum_rules COMMAND test_vacuum_rules)
add_test(NAME vacuum_sim COMMAND test_vacuum_sim)
add_test(NAME demo_rules COMMAND test_demo_rules)
add_test(NAME demo_sims COMMAND test_demo_sims)
