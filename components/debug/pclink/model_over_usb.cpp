// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * "load <bytes> <crc32>": the weights from the PC, for a board without them on its SD card. "ok send", the raw bytes,
 * then "ok load <bytes> in <ms> ms" once the model is ready, or "err load ...".
 */
#include <cstdlib>
#include "asr_service.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pclink.hpp"
#include "pclink_internal.hpp"

namespace pclink {

/* the screen's progress bar while the bytes come */
static void progress_task(void *arg)
{
    const size_t n = reinterpret_cast<size_t>(arg);
    while (asr::state() == asr::LOADING_MODEL) {
        asr::model_load_progress(static_cast<float>(received()) / n);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelete(nullptr);
}

void cmd_load(int, char **argv)
{
    const size_t n = strtoul(argv[1], nullptr, 0);
    const uint32_t crc = strtoul(argv[2], nullptr, 0);
    if (asr::model_loaded()) {
        reply("ok load (already loaded)");
        return;
    }
    uint8_t *dst = asr::model_load_begin(n, "USB");
    if (!dst) {
        reply("err load: no memory for %u bytes", (unsigned)n);
        return;
    }
    const int64_t t0 = esp_timer_get_time();
    xTaskCreate(progress_task, "load", 2048, reinterpret_cast<void *>(n), 3, nullptr);
    const bool got = receive(dst, n, 120000);
    const size_t k = received();
    if (asr::model_load_end(got, crc))
        reply("ok load %u bytes in %d ms", (unsigned)n, (int)((esp_timer_get_time() - t0) / 1000));
    else if (!got)
        reply("err load: %u of %u bytes", (unsigned)k, (unsigned)n);
    else if (asr::state() == asr::FAILED)
        reply("err load: %s", asr::failure());
    else
        reply("err load crc");
}

} // namespace pclink
