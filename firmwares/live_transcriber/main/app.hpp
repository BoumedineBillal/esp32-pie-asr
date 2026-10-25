// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The files this firmware reads from the SD card's top folder.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace app {

/* the weights (models/download.py puts them on the card); another size or CRC-32 is refused */
constexpr const char *MODEL_FILE = "kroko-en-q24-v1.bin";
constexpr size_t MODEL_BYTES = 26651392;
constexpr uint32_t MODEL_CRC = 0xbd3ff13c;
/* the phrase list, written with phrases/default.txt when the card has none */
constexpr const char *PHRASES_FILE = "phrases.txt";

} // namespace app
