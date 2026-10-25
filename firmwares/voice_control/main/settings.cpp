// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * settings.hpp: one NVS blob (namespace "voice_ctl", key "settings", with a version so a changed struct starts over).
 */
#include "settings.hpp"
#include "nvs.h"
#include "nvs_flash.h"

namespace settings {

static constexpr uint32_t VERSION = 1;
static Settings s_settings;

struct Stored {
    uint32_t version;
    Settings s;
};

void load()
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    nvs_handle_t h;
    if (nvs_open("voice_ctl", NVS_READONLY, &h) != ESP_OK)
        return;
    Stored st;
    size_t n = sizeof st;
    if (nvs_get_blob(h, "settings", &st, &n) == ESP_OK && n == sizeof st && st.version == VERSION)
        s_settings = st.s;
    nvs_close(h);
}

void save()
{
    nvs_handle_t h;
    if (nvs_open("voice_ctl", NVS_READWRITE, &h) != ESP_OK)
        return;
    Stored st = {VERSION, s_settings};
    nvs_set_blob(h, "settings", &st, sizeof st);
    nvs_commit(h);
    nvs_close(h);
}

Settings &get() { return s_settings; }

} // namespace settings
