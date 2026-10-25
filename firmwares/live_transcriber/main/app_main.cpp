// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * live_transcriber: the board's microphone to text on its screen, as you speak. The order matters for the PSRAM: the
 * screen first (its 1.2 MB frame buffer in one piece), then the service (the engine's state, the model's 26.65 MB
 * from the SD card, the engine's own 4.0 MB, the stream's buffers).
 */
#include <cstdio>
#include "app.hpp"
#include "asr_service.hpp"
#include "sdkconfig.h"
#include "transcript.hpp"
#include "ui/ui.hpp"

extern const char default_phrases_start[] asm("_binary_default_txt_start");

#if CONFIG_LIVE_TRANSCRIBER_DEBUG_LINK
void debug_link_start(); /* debug/commands.cpp */
#endif

extern "C" void app_main(void)
{
    printf("esp32-pie-asr live_transcriber, by Boumedine Billal: https://github.com/BoumedineBillal/esp32-pie-asr\n");
    asr::Config c;
    c.model_file = app::MODEL_FILE;
    c.model_bytes = app::MODEL_BYTES;
    c.model_crc = app::MODEL_CRC;
    c.phrases_file = app::PHRASES_FILE;
    c.default_phrases = default_phrases_start;
    c.beam = 4;
    asr::configure(c);
#if CONFIG_LIVE_TRANSCRIBER_DEBUG_LINK
    debug_link_start();
#endif
    transcript::init();
    if (!ui::start())
        printf("the display did not open\n");
    asr::run();
}
