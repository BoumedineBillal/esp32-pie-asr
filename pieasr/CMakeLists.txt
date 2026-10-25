# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

# The engine as an ESP-IDF component (ESP32-P4): the C sources, the PIE kernels in place of their C references, the
# two-core runtime. host/CMakeLists.txt builds the same sources for the host with the C references.
idf_component_register(
    SRCS
        src/model.c
        src/weights/weights_file.c
        src/weights/weights_prepare.c
        src/weights/weights_prepare_int.c
        src/encoder/encoder.c
        src/encoder/front_end.c
        src/encoder/layer.c
        src/encoder/feed_forward.c
        src/encoder/attention_weights.c
        src/encoder/attention_values.c
        src/encoder/conv_module.c
        src/encoder/norm.c
        src/decoder/decoder.c
        src/decoder/joiner.c
        src/decoder/greedy.c
        src/search/beam.c
        src/search/commit.c
        src/search/phrase_trie.c
        src/search/phrase_text.c
        src/search/phrase_file.c
        src/features/fbank.c
        src/math/fastmath.c
        src/math/tables/levels.c
        src/math/tables/scales.c
        src/math/tables/act.c
        src/math/tables/actch.c
        src/kernels/linear_ref.c
        src/kernels/linear_pie.c
        src/kernels/linear_int.c
        src/kernels/dense_int8.c
        src/kernels/pie/decode.S
        src/kernels/pie/tile.S
        src/kernels/pie/hadamard.S
        src/kernels/pie/output.S
        src/kernels/pie/depthwise.S
        src/kernels/pie/elementwise.S
        src/runtime/parallel_esp32.c
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src
    PRIV_REQUIRES freertos
    LDFRAGMENTS linker/pieasr.lf)

# -ffp-contract=off: no fused multiply-adds, so the chip and the host round alike (the host checks are bit for bit)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffp-contract=off)
# the kernels from pie/*.S
target_compile_definitions(${COMPONENT_LIB} PRIVATE PQ_HAVE_PIE=1)
