// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * p4mic: the microphone of the ESP32-P4X-Function-EV-Board, light and fully controlled.
 *
 *     p4mic::Microphone mic;
 *     mic.open();                                   // the defaults: 16 kHz mono, the board's codec and pins
 *     int16_t pcm[1600];
 *     p4mic::BlockInfo info;
 *     mic.read(pcm, 1600, &info);                   // the next 100 ms, in order: info.first, info.time_us, info.lost
 *
 * The microphone is an analog electret capsule into the ES8311 codec; the P4 is the I2S master (MCLK 256 x Fs). The
 * codec is driven by p4mic's own code: its whole power-on register image, then the register sequence of Espressif's
 * esp_codec_dev 1.6.2, read back. A task of p4mic's moves each 30 ms of I2S data into a ring in
 * PSRAM; read() takes from it in order. On the ESP-IDF drivers only (i2s_std, i2c_master). p4mic never touches
 * GPIO53, the speaker amplifier's enable. No exceptions: calls return false or -1 and lastError() says why.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace p4mic {

/* the codec's three gain stages, in signal order */
struct GainStages {
    int pga_db = 30;          /* the analog preamplifier: 0-30 dB in 3 dB steps */
    int scale_db = 0;         /* the digital scale after the ADC: 0-42 dB in 6 dB steps */
    float volume_db = 0;      /* the ADC volume: -95.5 to +32 dB in 0.5 dB steps */
    float total() const { return pga_db + scale_db + volume_db; }
};

struct Config {
    /* Hz: 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100 or 48000 */
    int sample_rate = 16000;
    /* 16, or 24: the ADC's 24 bits (in 32-bit I2S slots); read() gives either width in both */
    int bits = 16;
    /* the gain from the microphone to the samples, spread by Microphone::split(): the analog preamplifier first (it
     * keeps the noise lowest), then the digital scale, then the volume. 42 dB (PGA +30, scale +12)
     * is the default for speech: test speech comes out at -48 dBFS, 12 dB above the level under
     * which it starts to suffer (-60) and 30 dB under clipping. Espressif's esp_codec_dev leaves 30 dB (PGA +30,
     * scale 0) */
    float gain_db = 42;
    /* the ring in PSRAM: how far a reader may fall behind before frames are lost (2 bytes a frame: 5 s = 160 KB). At
     * least 200 ms and 2400 frames (300 ms at 8 kHz) */
    int buffer_ms = 5000;
    /* the codec's start is not given to read(): for its first 250 ms it puts out a step (-18 dBFS and a DC offset of
     * -995 in the first 50 ms) before the room's level. The frames are still counted: the first
     * block's index is the frames skipped */
    int settle_ms = 300;
    /* the task that moves the I2S data into the ring: above the application's, so that the I2S never overflows */
    int task_priority = 15;
    int task_core = -1;                      /* -1: either core */
    /* the codec's digital loopback, for tests: the samples return what writeLoopback() sends to the codec's DAC
     * (its left channel), bit for bit, without any sound (the amplifier stays off) */
    bool loopback = false;
    /* the board (ESP32-P4X-Function-EV-Board) */
    int i2s_port = 0;
    int mclk_pin = 13, bclk_pin = 12, ws_pin = 10, din_pin = 11, dout_pin = 9;
    /* the codec's control bus: the camera sensor's and the touch panel's too; reused when it is already open */
    int i2c_port = 0, sda_pin = 7, scl_pin = 8;
    int i2c_hz = 400000;
    int codec_address = 0x18;
};

/* what read() says about the frames it gave */
struct BlockInfo {
    uint64_t first = 0;    /* the first frame's index: frames since the open, at sample_rate a second, lost ones
                            * included (so it is the frame's time on the microphone's clock) */
    int64_t time_us = 0;   /* when that frame reached the P4 (esp_timer_get_time()'s clock) */
    uint32_t lost = 0;     /* frames that are not the microphone's since the previous block: skipped because the
                            * reader fell more than buffer_ms behind (`first` jumps), or zeros in this block where
                            * the I2S lost them */
    float gain_db = 0;     /* the gain the block's first frame was taken with (a change takes effect within a few
                            * ms of the frame it is logged at) */
};

class Microphone {
public:
    Microphone() = default;
    ~Microphone() { close(); }
    Microphone(const Microphone &) = delete;
    Microphone &operator=(const Microphone &) = delete;

    bool open(const Config &c = Config());
    void close();
    bool isOpen() const { return st_ != nullptr; }

    /* the next `frames` frames in order, waiting up to timeout_ms for them; returns the frames given (fewer only at
     * the timeout), -1 on error. One task reads; others may use the rest of the API meanwhile. int16_t: 16-bit
     * samples (at Config::bits 24, the top 16 of the 24); int32_t: full scale +-2^31, the ADC's 24 bits at the top
     * (at Config::bits 16, the 16 bits at the top) */
    int read(int16_t *samples, int frames, BlockInfo *info = nullptr, uint32_t timeout_ms = 1000);
    int read(int32_t *samples, int frames, BlockInfo *info = nullptr, uint32_t timeout_ms = 1000);
    int available() const;                  /* frames waiting */
    void flush();                           /* drop what waits: the next read starts with the newest audio */

    /* the gain: the nearest the stages can make (0.5 dB steps), from -95.5 to +104 dB */
    bool setGain(float db);
    float gain() const;
    bool setGainStages(const GainStages &g);  /* each stage on its own (each rounded to its steps) */
    GainStages gainStages() const;
    static GainStages split(float db);        /* how setGain() spreads a gain over the stages */

    /* the loopback (Config::loopback): frames (left, right) for the codec's DAC, in the I2S's width (int16_t at
     * Config::bits 16, int32_t at 24); returns the frames written */
    int writeLoopback(const int16_t *lr, int frames, uint32_t timeout_ms);
    int writeLoopback(const int32_t *lr, int frames, uint32_t timeout_ms);

    /* the codec's registers, for tests and tools (a write here changes what the library set) */
    int codecRegister(int reg);
    bool setCodecRegister(int reg, int value);

    /* the index of the frame being sampled now (BlockInfo::first's clock): to place an event in the recording */
    uint64_t frameNow() const;

    /* measured since the open */
    double measuredRateHz() const;          /* frames per second from the I2S receive interrupts (0 before two) */
    uint32_t overflows() const;             /* I2S receive buffers lost (480 frames each): the task was late */
    uint64_t lostFrames() const;            /* all frames lost: the I2S's and the reader's */
    esp_err_t lastError() const { return err_; }

    struct State;                           /* internal */

private:
    template <typename T> int readAs(T *samples, int frames, BlockInfo *info, uint32_t timeout_ms);
    State *st_ = nullptr;
    esp_err_t err_ = ESP_OK;
};

} // namespace p4mic
