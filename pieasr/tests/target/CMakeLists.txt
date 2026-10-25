# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

# The PIE kernels against their C models on the chip, then their cycle counts; the pieasr component from ../../
cmake_minimum_required(VERSION 3.22)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../..)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_kernels)
