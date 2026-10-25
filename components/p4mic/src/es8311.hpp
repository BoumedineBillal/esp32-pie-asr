// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The ES8311 codec (internal): its registers over I2C, and the register states p4mic puts it in.
 *
 * The codec keeps its registers across a reset of the P4, and its soft reset (register 0x00) restores none of them
 * (measured). So every start writes the codec's whole power-on register image first, then the state
 * wanted, all as absolute values, and reads every register back.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include "driver/i2c_master.h"
#include "esp_err.h"

namespace p4mic::detail {

constexpr int ES8311_REGS = 0x46; /* registers 0x00-0x45 (the chip ID is at 0xFD-0xFF) */

class Es8311 {
public:
    esp_err_t attach(i2c_master_bus_handle_t bus, int address, int hz);
    void detach();
    esp_err_t write(int reg, int value);
    int read(int reg); /* -1: no answer */

    /* Espressif's ADC state (esp_codec_dev 1.6.2, ADC only, slave, MCLK = 256 x Fs on its pin, Philips, both slots
     * the ADC), from any earlier state. word_bits 16 or 32 (the ADC's 24 bits in a 32-bit word); sample rates
     * 8000-48000 from esp_codec_dev's table at 256 x Fs */
    esp_err_t startAdc(int sample_rate, int word_bits);
    static bool rateSupported(int sample_rate);
    /* the DAC path on, its left channel returned in the left ADC slot (the digital loopback; no sound: the
     * amplifier stays off) */
    esp_err_t startLoopback();
    /* Espressif's power-down sequence (its close) */
    esp_err_t stop();

private:
    /* writes the steps, then reads every register back against the image they make */
    esp_err_t apply(const uint8_t (*steps)[2], size_t n, uint8_t *image);
    i2c_master_dev_handle_t dev_ = nullptr;
    uint8_t image_[ES8311_REGS] = {};
};

} // namespace p4mic::detail
