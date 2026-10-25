# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

# The engine on the host (x86-64 Linux): the same C as the chip with the kernels' C references instead of the PIE
# assembly, the transcribe tool and the host tests.
#   cmake -S pieasr/host -B build/host [-DPIEASR_WEIGHTS=<weights file>]
#   cmake --build build/host && ctest --test-dir build/host
# -ffp-contract=off: no fused multiply-adds, so the host and the chip round alike (every result bit for bit).
cmake_minimum_required(VERSION 3.16)
project(pieasr_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(PIEASR_WEIGHTS "" CACHE FILEPATH "the model's weights file (for the golden test)")

set(E ${CMAKE_CURRENT_LIST_DIR}/..)
add_library(pieasr STATIC
    ${E}/src/model.c
    ${E}/src/weights/weights_file.c
    ${E}/src/weights/weights_prepare.c
    ${E}/src/weights/weights_prepare_int.c
    ${E}/src/encoder/encoder.c
    ${E}/src/encoder/front_end.c
    ${E}/src/encoder/layer.c
    ${E}/src/encoder/feed_forward.c
    ${E}/src/encoder/attention_weights.c
    ${E}/src/encoder/attention_values.c
    ${E}/src/encoder/conv_module.c
    ${E}/src/encoder/norm.c
    ${E}/src/decoder/decoder.c
    ${E}/src/decoder/joiner.c
    ${E}/src/decoder/greedy.c
    ${E}/src/search/beam.c
    ${E}/src/search/commit.c
    ${E}/src/search/phrase_trie.c
    ${E}/src/search/phrase_text.c
    ${E}/src/search/phrase_file.c
    ${E}/src/features/fbank.c
    ${E}/src/math/fastmath.c
    ${E}/src/math/tables/levels.c
    ${E}/src/math/tables/scales.c
    ${E}/src/math/tables/act.c
    ${E}/src/math/tables/actch.c
    ${E}/src/kernels/linear_ref.c
    ${E}/src/kernels/linear_pie.c
    ${E}/src/kernels/linear_int.c
    ${E}/src/kernels/dense_int8.c
    ${E}/src/kernels/c/decode.c
    ${E}/src/kernels/c/tile.c
    ${E}/src/kernels/c/hadamard.c
    ${E}/src/kernels/c/output.c
    ${E}/src/kernels/c/depthwise.c
    ${E}/src/kernels/c/elementwise.c
    ${E}/src/runtime/parallel_host.c)
target_include_directories(pieasr PUBLIC ${E}/include PRIVATE ${E}/src)
target_compile_options(pieasr PRIVATE -O2 -g -Wall -ffp-contract=off)
target_link_libraries(pieasr PUBLIC m)

add_executable(transcribe transcribe.c)
target_compile_options(transcribe PRIVATE -O2 -g -Wall -ffp-contract=off)
target_link_libraries(transcribe pieasr)

# the tests (they may include the engine's internal headers)
enable_testing()
foreach(t test_search test_phrases test_math test_golden)
    add_executable(${t} ${E}/tests/host/${t}.c)
    target_include_directories(${t} PRIVATE ${E}/src)
    target_compile_options(${t} PRIVATE -O2 -g -Wall -ffp-contract=off)
    target_link_libraries(${t} pieasr)
endforeach()
add_test(NAME search COMMAND test_search)
add_test(NAME phrases COMMAND test_phrases)
add_test(NAME math COMMAND test_math)
if(PIEASR_WEIGHTS)
    add_test(NAME golden COMMAND test_golden ${PIEASR_WEIGHTS} ${E}/tests/data)
endif()
