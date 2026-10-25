// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * p4sdcard.hpp: ESP-IDF's SDMMC host and FatFs (esp_vfs_fat_sdmmc_mount), the card powered by the on-chip LDO as on
 * this board (ESP-IDF's examples/storage/sd_card/sdmmc for the P4). Reads and writes go through a buffer in internal
 * DMA memory (64 KB, less if that much is not free), so the caller's memory can be anything (PSRAM included).
 */
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_vfs_fat.h"
#include "p4sdcard.hpp"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

namespace p4sdcard {

static const char *TAG = "p4sdcard";

/* the transfer buffer in internal DMA memory: 64 KB, or less when internal RAM is short (an app with little left) */
static uint8_t *chunk_buffer(size_t *n)
{
    for (*n = 64 * 1024; *n >= 4096; *n /= 2)
        if (void *b = heap_caps_malloc(*n, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL))
            return static_cast<uint8_t *>(b);
    return nullptr;
}

bool Card::open(const Config &c)
{
    close();
    sd_pwr_ctrl_handle_t pwr = nullptr;
    if (c.ldo_channel >= 0) {
        sd_pwr_ctrl_ldo_config_t ldo = {};
        ldo.ldo_chan_id = c.ldo_channel;
        if ((err_ = sd_pwr_ctrl_new_on_chip_ldo(&ldo, &pwr)) != ESP_OK) {
            ESP_LOGE(TAG, "the card's power (LDO %d): %s", c.ldo_channel, esp_err_to_name(err_));
            return false;
        }
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = c.freq_khz;
    host.pwr_ctrl_handle = pwr;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = c.width;
#ifdef CONFIG_SOC_SDMMC_USE_GPIO_MATRIX
    slot.clk = static_cast<gpio_num_t>(c.clk_pin);
    slot.cmd = static_cast<gpio_num_t>(c.cmd_pin);
    slot.d0 = static_cast<gpio_num_t>(c.d0_pin);
    slot.d1 = static_cast<gpio_num_t>(c.d1_pin);
    slot.d2 = static_cast<gpio_num_t>(c.d2_pin);
    slot.d3 = static_cast<gpio_num_t>(c.d3_pin);
#endif
    esp_vfs_fat_sdmmc_mount_config_t mc = {};
    mc.format_if_mount_failed = false; /* never: a card's files are the user's */
    mc.max_files = c.max_files;
    mc.allocation_unit_size = 16 * 1024;
    sdmmc_card_t *card = nullptr;
    snprintf(mount_, sizeof mount_, "%s", c.mount_point);
    if ((err_ = esp_vfs_fat_sdmmc_mount(mount_, &host, &slot, &mc, &card)) != ESP_OK) {
        ESP_LOGW(TAG, "no card, or no FAT on it: %s", esp_err_to_name(err_));
        if (pwr)
            sd_pwr_ctrl_del_on_chip_ldo(pwr);
        return false;
    }
    card_ = card;
    pwr_ = pwr;
    return true;
}

void Card::close()
{
    if (card_)
        esp_vfs_fat_sdcard_unmount(mount_, static_cast<sdmmc_card_t *>(card_));
    if (pwr_)
        sd_pwr_ctrl_del_on_chip_ldo(static_cast<sd_pwr_ctrl_handle_t>(pwr_));
    card_ = pwr_ = nullptr;
}

Info Card::info() const
{
    Info i = {};
    if (!card_)
        return i;
    const sdmmc_card_t *c = static_cast<const sdmmc_card_t *>(card_);
    snprintf(i.name, sizeof i.name, "%s", c->cid.name);
    i.bytes = static_cast<uint64_t>(c->csd.capacity) * c->csd.sector_size;
    i.freq_khz = c->real_freq_khz;
    return i;
}

bool Card::full(const char *path, char *out, size_t n) const
{
    return card_ && snprintf(out, n, "%s/%s", mount_, path) < static_cast<int>(n);
}

long Card::fileSize(const char *path) const
{
    char p[256];
    struct stat st;
    if (!full(path, p, sizeof p) || stat(p, &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    return static_cast<long>(st.st_size);
}

long Card::read(const char *path, void *dst, size_t max, uint32_t *crc,
                void (*progress)(size_t, size_t, void *), void *arg)
{
    char p[256];
    if (!full(path, p, sizeof p)) {
        err_ = ESP_ERR_INVALID_STATE;
        return -1;
    }
    const int fd = ::open(p, O_RDONLY);
    if (fd < 0) {
        err_ = ESP_ERR_NOT_FOUND;
        return -1;
    }
    size_t chunk;
    uint8_t *buf = chunk_buffer(&chunk);
    if (!buf) {
        ::close(fd);
        err_ = ESP_ERR_NO_MEM;
        return -1;
    }
    const long total = fileSize(path);
    const size_t want = total >= 0 && static_cast<size_t>(total) < max ? static_cast<size_t>(total) : max;
    size_t done = 0;
    uint32_t c = 0;
    while (done < want) {
        const size_t k = want - done < chunk ? want - done : chunk;
        const ssize_t r = ::read(fd, buf, k);
        if (r <= 0)
            break;
        memcpy(static_cast<uint8_t *>(dst) + done, buf, r);
        c = esp_rom_crc32_le(c, buf, r);
        done += r;
        if (progress)
            progress(done, want, arg);
    }
    heap_caps_free(buf);
    ::close(fd);
    if (done != want) {
        err_ = ESP_FAIL;
        return -1;
    }
    if (crc)
        *crc = c;
    err_ = ESP_OK;
    return static_cast<long>(done);
}

bool Card::write(const char *path, const void *src, size_t n)
{
    char p[256];
    if (!full(path, p, sizeof p)) {
        err_ = ESP_ERR_INVALID_STATE;
        return false;
    }
    const int fd = ::open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        err_ = ESP_FAIL;
        return false;
    }
    size_t chunk;
    uint8_t *buf = chunk_buffer(&chunk);
    size_t done = 0;
    while (buf && done < n) {
        const size_t k = n - done < chunk ? n - done : chunk;
        memcpy(buf, static_cast<const uint8_t *>(src) + done, k);
        if (::write(fd, buf, k) != static_cast<ssize_t>(k))
            break;
        done += k;
    }
    heap_caps_free(buf);
    const bool ok = ::close(fd) == 0 && done == n && buf;
    err_ = ok ? ESP_OK : ESP_FAIL;
    return ok;
}

bool Card::copy(const char *from, const char *to, uint32_t *crc)
{
    char a[256], b[256];
    if (!full(from, a, sizeof a) || !full(to, b, sizeof b)) {
        err_ = ESP_ERR_INVALID_STATE;
        return false;
    }
    const int in = ::open(a, O_RDONLY);
    if (in < 0) {
        err_ = ESP_ERR_NOT_FOUND;
        return false;
    }
    const int out = ::open(b, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    size_t chunk;
    uint8_t *buf = out >= 0 ? chunk_buffer(&chunk) : nullptr;
    bool ok = buf != nullptr;
    uint32_t c = 0;
    while (ok) {
        const ssize_t r = ::read(in, buf, chunk);
        if (r <= 0) {
            ok = r == 0;
            break;
        }
        ok = ::write(out, buf, r) == r;
        c = esp_rom_crc32_le(c, buf, r);
    }
    heap_caps_free(buf);
    ::close(in);
    if (out >= 0)
        ok = ::close(out) == 0 && ok;
    if (ok && crc)
        *crc = c;
    err_ = ok ? ESP_OK : ESP_FAIL;
    return ok;
}

bool Card::remove(const char *path)
{
    char p[256];
    const bool ok = full(path, p, sizeof p) && unlink(p) == 0;
    err_ = ok ? ESP_OK : ESP_ERR_NOT_FOUND;
    return ok;
}

int Card::list(void (*fn)(const char *, long, void *), void *arg, const char *dir) const
{
    char p[256];
    if (!full(dir, p, sizeof p))
        return -1;
    DIR *d = opendir(p);
    if (!d)
        return -1;
    int n = 0;
    while (struct dirent *e = readdir(d)) {
        char f[600];
        struct stat st;
        const bool fits = snprintf(f, sizeof f, "%s/%s", p, e->d_name) < static_cast<int>(sizeof f);
        const long size = fits && stat(f, &st) == 0 && S_ISREG(st.st_mode) ? static_cast<long>(st.st_size) : -1;
        if (fn)
            fn(e->d_name, size, arg);
        n++;
    }
    closedir(d);
    return n;
}

} // namespace p4sdcard
