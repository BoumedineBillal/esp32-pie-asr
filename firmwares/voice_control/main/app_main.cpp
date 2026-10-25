// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * voice_control: the transcriber of live_transcriber, and voice-control demos (a robot vacuum, a smart home, a car
 * cabin, a factory line, a kitchen, a presenter, each driven by spoken orders through word rules), behind a home menu.
 * The order matters for the PSRAM: the screen first (its 1.2 MB frame buffer in one piece), then the service (the
 * engine's state, the model's 26.65 MB from the SD card, the engine's own 4.0 MB, the stream's buffers).
 */
#include <cstdio>
#include "app.hpp"
#include "asr_service.hpp"
#include "sdkconfig.h"
#include "transcript.hpp"
#include "ui/ui.hpp"

extern const char default_phrases_start[] asm("_binary_default_txt_start");
extern const char vacuum_phrases_start[] asm("_binary_vacuum_txt_start");
extern const char smarthome_phrases_start[] asm("_binary_smarthome_txt_start");
extern const char car_phrases_start[] asm("_binary_car_txt_start");
extern const char factory_phrases_start[] asm("_binary_factory_txt_start");
extern const char kitchen_phrases_start[] asm("_binary_kitchen_txt_start");
extern const char presenter_phrases_start[] asm("_binary_presenter_txt_start");

#if CONFIG_VOICE_CONTROL_DEBUG_LINK
void debug_link_start(); /* debug/commands.cpp */
#endif

extern "C" void app_main(void)
{
    printf("esp32-pie-asr voice_control, by Boumedine Billal: https://github.com/BoumedineBillal/esp32-pie-asr\n");
    asr::Config c;
    c.model_file = app::MODEL_FILE;
    c.model_bytes = app::MODEL_BYTES;
    c.model_crc = app::MODEL_CRC;
    c.phrases_file = app::PHRASES_FILE;
    c.default_phrases = default_phrases_start;
    const char *const defaults[6] = {vacuum_phrases_start,  smarthome_phrases_start, car_phrases_start,
                                     factory_phrases_start, kitchen_phrases_start,   presenter_phrases_start};
    for (int i = 0; i < 6; i++) { /* list 1 + i: demo i's */
        c.more_phrase_files[i] = app::DEMO_PHRASE_FILES[i];
        c.more_default_phrases[i] = defaults[i];
    }
    c.beam = 4;
    asr::configure(c);
#if CONFIG_VOICE_CONTROL_DEBUG_LINK
    debug_link_start();
#endif
    transcript::init();
    if (!ui::start())
        printf("the display did not open\n");
    asr::run();
}
