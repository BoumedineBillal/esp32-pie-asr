// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * usb_stream (usb_stream.hpp): one vendor-specific interface with a bulk IN and a bulk OUT endpoint, and the
 * descriptors that make Windows load WinUSB for it: USB 2.1, a BOS descriptor announcing Microsoft OS 2.0
 * descriptors, and those descriptors (compatible ID "WINUSB", the device interface GUID).
 */
#include <cstring>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "usb_stream.hpp"

namespace usb_stream {

static const char *TAG = "usb_stream";
constexpr uint8_t EP_OUT = 0x01, EP_IN = 0x81;
constexpr uint8_t VENDOR_REQ_MS_OS_20 = 0x01; /* the request the PC sends to get the Microsoft OS 2.0 descriptors */
constexpr uint16_t MS_OS_20_LEN = 10 + 20 + 132;

static const tusb_desc_device_t s_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210, /* 2.1: the PC then asks for the BOS descriptor */
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4020, /* TinyUSB's product ID for a vendor-only device on Espressif's vendor ID */
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const tusb_desc_device_qualifier_t s_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0210,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 1,
    .bReserved = 0,
};

constexpr uint16_t CONFIG_LEN = TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN;
static const uint8_t s_config_hs[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_LEN, 0, 100),
    TUD_VENDOR_DESCRIPTOR(0, 4, EP_OUT, EP_IN, 512),
};
static const uint8_t s_config_fs[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_LEN, 0, 100),
    TUD_VENDOR_DESCRIPTOR(0, 4, EP_OUT, EP_IN, 64),
};

static const char *s_strings[] = {"\x09\x04", "esp32-pie-asr", "esp32-pie-asr debug link", "000001", "debug link"};

static const uint8_t s_bos[] = {
    TUD_BOS_DESCRIPTOR(TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_LEN, VENDOR_REQ_MS_OS_20),
};

/* the Microsoft OS 2.0 descriptor set, for the whole (single-interface) device: the set header, the compatible ID
 * "WINUSB", the registry property DeviceInterfaceGUIDs (REG_MULTI_SZ) */
static uint8_t s_ms_os_20[MS_OS_20_LEN];

static void put16(uint8_t *&p, uint16_t v)
{
    *p++ = v & 0xff;
    *p++ = v >> 8;
}

static void put_utf16(uint8_t *&p, const char *s, int nuls)
{
    while (*s)
        put16(p, static_cast<uint8_t>(*s++));
    while (nuls--)
        put16(p, 0);
}

static size_t build_ms_os_20()
{
    uint8_t *p = s_ms_os_20;
    put16(p, 10);
    put16(p, MS_OS_20_SET_HEADER_DESCRIPTOR);
    put16(p, 0x0000); /* Windows 8.1 and later: 0x06030000 */
    put16(p, 0x0603);
    put16(p, MS_OS_20_LEN);
    put16(p, 20);
    put16(p, MS_OS_20_FEATURE_COMPATBLE_ID);
    memcpy(p, "WINUSB\0\0", 8);
    p += 8;
    memset(p, 0, 8); /* no sub-compatible ID */
    p += 8;
    put16(p, 132);
    put16(p, MS_OS_20_FEATURE_REG_PROPERTY);
    put16(p, 7);  /* REG_MULTI_SZ */
    put16(p, 42); /* the name's length: 21 UTF-16 characters */
    put_utf16(p, "DeviceInterfaceGUIDs", 1);
    put16(p, 80); /* the value's length: 38 characters and two NULs */
    put_utf16(p, INTERFACE_GUID, 2);
    return p - s_ms_os_20;
}

static void (*s_on_rx)(const uint8_t *, size_t);
static TaskHandle_t s_waiter; /* the task in send(), woken when a transfer to the PC completes */

} // namespace usb_stream

/* TinyUSB's callbacks (weak C functions it calls by name), in its own task */
extern "C" uint8_t const *tud_descriptor_bos_cb(void) { return usb_stream::s_bos; }

extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *r)
{
    if (stage != CONTROL_STAGE_SETUP)
        return true;
    if (r->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR && r->bRequest == usb_stream::VENDOR_REQ_MS_OS_20 &&
        r->wIndex == 7) /* 7: MS_OS_20_DESCRIPTOR_INDEX */
        return tud_control_xfer(rhport, r, usb_stream::s_ms_os_20, sizeof(usb_stream::s_ms_os_20));
    return false; /* anything else: stall */
}

extern "C" void tud_vendor_tx_cb(uint8_t idx, uint32_t sent_bytes)
{
    if (usb_stream::s_waiter)
        xTaskNotifyGive(usb_stream::s_waiter);
}

#if CFG_TUD_API_V0_19_COMPAT /* esp_tinyusb sets it: the length is 16 bits */
extern "C" void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint16_t bufsize)
#else
extern "C" void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint32_t bufsize)
#endif
{
    uint8_t buf[64];
    uint32_t n;
    while ((n = tud_vendor_n_read(idx, buf, sizeof(buf))) > 0)
        if (usb_stream::s_on_rx)
            usb_stream::s_on_rx(buf, n);
}

namespace usb_stream {

bool start(void (*on_rx)(const uint8_t *, size_t))
{
    if (build_ms_os_20() != MS_OS_20_LEN) {
        ESP_LOGE(TAG, "the Microsoft OS 2.0 descriptor set has the wrong length");
        return false;
    }
    s_on_rx = on_rx;
    tinyusb_config_t tusb = TINYUSB_DEFAULT_CONFIG();
    tusb.descriptor.device = &s_device;
    tusb.descriptor.qualifier = &s_qualifier;
    tusb.descriptor.string = s_strings;
    tusb.descriptor.string_count = sizeof(s_strings) / sizeof(s_strings[0]);
    tusb.descriptor.full_speed_config = s_config_fs;
    tusb.descriptor.high_speed_config = s_config_hs;
    if (tinyusb_driver_install(&tusb) != ESP_OK) {
        ESP_LOGE(TAG, "USB (TinyUSB, a vendor interface on the HS port) did not start");
        return false;
    }
    return true;
}

bool mounted() { return tud_vendor_n_mounted(0); }

/* each write at most the FIFO's free space (TinyUSB takes the length as 16 bits), a flush, then wait for a
 * transfer to complete */
bool send(const void *data, size_t len)
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    s_waiter = xTaskGetCurrentTaskHandle();
    while (len) {
        if (!tud_vendor_n_mounted(0))
            return false;
        uint32_t room = tud_vendor_n_write_available(0);
        uint32_t n = room ? tud_vendor_n_write(0, p, len < room ? len : room) : 0;
        p += n;
        len -= n;
        tud_vendor_n_write_flush(0);
        if (len)
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    }
    return true;
}

} // namespace usb_stream
