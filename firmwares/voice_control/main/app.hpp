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
/* the transcriber's phrase list (phrase list 0), written with phrases/default.txt when the card has none */
constexpr const char *PHRASES_FILE = "phrases.txt";
/* each demo's (phrase list 1 + its index: vacuum, smarthome, car, factory, kitchen, presenter), written with
 * phrases/<demo>.txt when the card has none */
constexpr const char *DEMO_PHRASE_FILES[6] = {"vacuum_phrases.txt",  "smarthome_phrases.txt", "car_phrases.txt",
                                              "factory_phrases.txt", "kitchen_phrases.txt",   "presenter_phrases.txt"};

} // namespace app
