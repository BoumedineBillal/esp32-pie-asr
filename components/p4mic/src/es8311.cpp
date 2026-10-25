// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0

/*
 * The ES8311's register states (es8311.hpp).
 *
 * The ADC sequence and the power-down sequence are the register writes Espressif's esp_codec_dev 1.6.2 makes for
 * this board's microphone (src/device/es8311/es8311.c: es8311_open, es8311_set_fs, es8311_start, es8311_suspend,
 * and esp_codec_dev_open's gain write), recorded on the board over I2C and written here as
 * absolute values. Copyright (c) 2023 Espressif Systems (Shanghai) CO LTD, Apache-2.0.
 */
#include "es8311.hpp"
#include <cstring>
#include "esp_log.h"

namespace p4mic::detail {

static const char *TAG = "p4mic";
static constexpr int I2C_TIMEOUT_MS = 50;

/* the registers 0x00-0x45 as the codec powers up (read on the board before any program had written them; every
 * default the Everest user guide gives agrees) */
static const uint8_t POWER_ON[ES8311_REGS] = {
    0x1F, 0x00, 0x00, 0x10, 0x10, 0x00, 0x03, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x20, 0xFC, 0x6A, 0x00, /* 0x00 */
    0x13, 0x7C, 0x02, 0x40, 0x10, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x4C, 0x00, 0x00, 0x00, /* 0x10 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x20 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x30 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                                                             /* 0x40 */
};

/* esp_codec_dev 1.6.2's writes for the ADC at 16 kHz from the power-on state, in its order (45 writes) */
static const uint8_t ADC_16K[][2] = {
    /* es8311_open */
    {0x0D, 0xFA}, {0x44, 0x08}, {0x44, 0x08}, {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x16, 0x24},
    {0x04, 0x10}, {0x05, 0x00}, {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F}, {0x00, 0x80},
    {0x00, 0x80}, {0x01, 0x3F}, {0x06, 0x03}, {0x13, 0x10}, {0x1B, 0x0A}, {0x1C, 0x6A}, {0x44, 0x08},
    /* es8311_set_fs: 16-bit Philips; 16 kHz from MCLK 4.096 MHz (pre_div 1, x1, OSR 0x10 / 0x20, LRCK 256) */
    {0x09, 0x0C}, {0x0A, 0x0C}, {0x09, 0x0C}, {0x0A, 0x0C}, {0x02, 0x00}, {0x05, 0x00}, {0x03, 0x10},
    {0x04, 0x20}, {0x07, 0x00}, {0x08, 0xFF}, {0x06, 0x03},
    /* es8311_start: slave, MCLK from its pin, ADC unmuted, ADC volume 0 dB, analog on, PGA +30 dB on MIC1 */
    {0x00, 0x80}, {0x01, 0x3F}, {0x09, 0x0C}, {0x0A, 0x0C}, {0x17, 0xBF}, {0x0E, 0x02}, {0x14, 0x1A},
    {0x14, 0x1A}, {0x0D, 0x01}, {0x15, 0x40}, {0x37, 0x08}, {0x45, 0x00},
    /* esp_codec_dev_open: the stored mic gain (0) into the ADC scale */
    {0x16, 0x00},
};

/* the DAC powered and unmuted at 0 dB, the ADC slot mux to DAC-left + ADC (the loopback is taken before the DAC
 * volume: measured) */
static const uint8_t LOOPBACK[][2] = {{0x12, 0x00}, {0x31, 0x00}, {0x32, 0xBF}, {0x44, 0x48}};

/* esp_codec_dev 1.6.2's es8311_suspend (its close), after the DAC muted and powered down and the slot mux back */
static const uint8_t STOP[][2] = {
    {0x31, 0x60}, {0x44, 0x08}, {0x32, 0x00}, {0x17, 0x00}, {0x0E, 0xFF}, {0x12, 0x02}, {0x14, 0x00},
    {0x0D, 0xFA}, {0x15, 0x00}, {0x02, 0x10}, {0x00, 0x00}, {0x00, 0x1F}, {0x01, 0x30}, {0x01, 0x00},
    {0x45, 0x00}, {0x0D, 0xFC}, {0x02, 0x00},
};

esp_err_t Es8311::attach(i2c_master_bus_handle_t bus, int address, int hz)
{
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = static_cast<uint16_t>(address);
    dc.scl_speed_hz = hz;
    return i2c_master_bus_add_device(bus, &dc, &dev_);
}

void Es8311::detach()
{
    if (dev_)
        i2c_master_bus_rm_device(dev_);
    dev_ = nullptr;
}

esp_err_t Es8311::write(int reg, int value)
{
    uint8_t b[2] = {static_cast<uint8_t>(reg), static_cast<uint8_t>(value)};
    return i2c_master_transmit(dev_, b, 2, I2C_TIMEOUT_MS);
}

int Es8311::read(int reg)
{
    uint8_t a = static_cast<uint8_t>(reg), v = 0;
    return i2c_master_transmit_receive(dev_, &a, 1, &v, 1, I2C_TIMEOUT_MS) == ESP_OK ? v : -1;
}

esp_err_t Es8311::apply(const uint8_t (*steps)[2], size_t n, uint8_t *image)
{
    for (size_t i = 0; i < n; i++) {
        esp_err_t e = write(steps[i][0], steps[i][1]);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "the codec: writing 0x%02x: %s", steps[i][0], esp_err_to_name(e));
            return e;
        }
        image[steps[i][0]] = steps[i][1];
    }
    /* every register as it should be; a value that did not take is written once more (Espressif writes 0x44 twice
     * for "occasional failures during the first I2C write") */
    for (int r = 0; r < ES8311_REGS; r++) {
        if (read(r) == image[r])
            continue;
        write(r, image[r]);
        int v = read(r);
        if (v != image[r]) {
            ESP_LOGE(TAG, "the codec: register 0x%02x reads 0x%02x, written 0x%02x", r, v & 0xff, image[r]);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

/* esp_codec_dev 1.6.2's clock table (es8311.c coeff_div) at MCLK = 256 x Fs: every rate has pre_div 1, x1, ADC and
 * DAC dividers 1, single speed, LRCK 256, BCLK / 4, ADC OSR 0x10; only the DAC's OSR differs (0x20 up to 16 kHz,
 * 0x10 above). So the ADC sequence is the same for every rate but register 0x04 */
static const struct {
    int rate;
    uint8_t dac_osr;
} RATES[] = {{8000, 0x20},  {11025, 0x20}, {12000, 0x20}, {16000, 0x20}, {22050, 0x10},
             {24000, 0x10}, {32000, 0x10}, {44100, 0x10}, {48000, 0x10}};

bool Es8311::rateSupported(int sample_rate)
{
    for (const auto &r : RATES)
        if (r.rate == sample_rate)
            return true;
    return false;
}

esp_err_t Es8311::startAdc(int sample_rate, int word_bits)
{
    int dac_osr = -1;
    for (const auto &r : RATES)
        if (r.rate == sample_rate)
            dac_osr = r.dac_osr;
    if (dac_osr < 0 || (word_bits != 16 && word_bits != 32))
        return ESP_ERR_NOT_SUPPORTED;
    uint8_t power_on[ES8311_REGS][2];
    for (int r = 0; r < ES8311_REGS; r++) {
        power_on[r][0] = static_cast<uint8_t>(r);
        power_on[r][1] = POWER_ON[r];
    }
    memcpy(image_, POWER_ON, sizeof image_);
    esp_err_t e = apply(power_on, ES8311_REGS, image_);
    if (e != ESP_OK)
        return e;
    /* the 16 kHz 16-bit sequence with the rate's DAC OSR and the word length (0x09 / 0x0A bits 4:2: 3 = 16-bit,
     * 4 = 32-bit; esp_codec_dev ORs them into what the register holds, here they are written whole) */
    uint8_t seq[sizeof ADC_16K / sizeof ADC_16K[0]][2];
    memcpy(seq, ADC_16K, sizeof seq);
    for (auto &s : seq) {
        if (s[0] == 0x04 && s[1] == 0x20)
            s[1] = static_cast<uint8_t>(dac_osr);
        if (s[0] == 0x09 || s[0] == 0x0A)
            s[1] = word_bits == 32 ? 0x10 : 0x0C;
    }
    return apply(seq, sizeof seq / sizeof seq[0], image_);
}

esp_err_t Es8311::startLoopback() { return apply(LOOPBACK, sizeof LOOPBACK / sizeof LOOPBACK[0], image_); }

esp_err_t Es8311::stop()
{
    for (const auto &s : STOP)
        write(s[0], s[1]);
    return ESP_OK;
}

} // namespace p4mic::detail
