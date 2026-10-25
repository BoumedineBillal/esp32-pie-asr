# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22)
set(REPO ${CMAKE_CURRENT_LIST_DIR}/../..)
set(EXTRA_COMPONENT_DIRS ${REPO}/pieasr ${REPO}/components ${REPO}/components/debug)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(live_transcriber)
