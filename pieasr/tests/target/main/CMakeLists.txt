# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

idf_component_register(SRCS "test_kernels.c" "test_int_kernels.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES pieasr esp_timer heap)
# the engine's internal headers (kernels.h, linear.h)
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../../src)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffp-contract=off -Wno-format)
