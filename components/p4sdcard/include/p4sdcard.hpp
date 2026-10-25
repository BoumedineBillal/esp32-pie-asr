// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * p4sdcard: the microSD card of the ESP32-P4X-Function-EV-Board, light and fully controlled.
 *
 *     p4sdcard::Card sd;
 *     sd.open();                                       // the card powered, SDMMC 4-bit at 40 MHz, FAT mounted on /sd
 *     long n = sd.fileSize("model.bin");               // -1 when it is not there
 *     uint32_t crc;
 *     sd.read("model.bin", buf, n, &crc);              // the whole file into buf (any memory), its CRC-32
 *     sd.close();
 *
 * On the ESP-IDF drivers only (the SDMMC host, the on-chip LDO that powers the card on this board, FatFs). No
 * exceptions: calls return false or -1 and lastError() says why.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace p4sdcard {

struct Config {
    const char *mount_point = "/sd";
    int freq_khz = 40000;    /* 40 MHz high speed; 20000 the default speed (for a card that fails at 40) */
    int width = 4;           /* data lines: 4 or 1 */
    int ldo_channel = 4;     /* the on-chip LDO that powers the card on this board (-1: powered from elsewhere) */
    int clk_pin = 43, cmd_pin = 44, d0_pin = 39, d1_pin = 40, d2_pin = 41, d3_pin = 42; /* slot 0 on this board */
    int max_files = 4;       /* files open at once */
};

struct Info {
    char name[8];            /* the card's product name */
    uint64_t bytes;          /* its capacity */
    int freq_khz;            /* the bus clock it runs at */
};

class Card {
public:
    Card() = default;
    ~Card() { close(); }
    Card(const Card &) = delete;
    Card &operator=(const Card &) = delete;

    /* powers the card, finds it and mounts its FAT; false when there is no card (or no FAT on it) */
    bool open(const Config &c = Config());
    void close();
    bool isOpen() const { return card_ != nullptr; }
    Info info() const;

    /* paths are inside the card ("dir/file.bin") */
    long fileSize(const char *path) const;   /* -1: no such file */
    /* the file's first max bytes into dst (internal RAM or PSRAM); returns the bytes read, or -1. crc: zlib's CRC-32
     * of them. progress(done, total, arg) after each chunk (64 KB, less when internal RAM is short) */
    long read(const char *path, void *dst, size_t max, uint32_t *crc = nullptr,
              void (*progress)(size_t done, size_t total, void *arg) = nullptr, void *arg = nullptr);
    bool write(const char *path, const void *src, size_t n);   /* creates or replaces it */
    /* a file copied to another (created or replaced), through the transfer buffer; crc: zlib's CRC-32 of the copy */
    bool copy(const char *from, const char *to, uint32_t *crc = nullptr);
    bool remove(const char *path);
    /* the entries of a folder ("" the top): fn(name, size or -1 for a folder, arg); returns how many */
    int list(void (*fn)(const char *name, long size, void *arg), void *arg, const char *dir = "") const;

    esp_err_t lastError() const { return err_; }

private:
    bool full(const char *path, char *out, size_t n) const;
    void *card_ = nullptr, *pwr_ = nullptr;
    char mount_[16] = {};
    esp_err_t err_ = ESP_OK;
};

} // namespace p4sdcard
