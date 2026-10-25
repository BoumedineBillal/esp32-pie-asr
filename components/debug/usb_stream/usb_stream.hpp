// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * usb_stream: a byte stream to the PC over USB High-Speed, with a few bytes back.
 *
 * The board is a vendor-specific USB device that Windows binds to its own WinUSB driver by itself (Microsoft OS 2.0
 * descriptors: nothing to install). Not a serial port (CDC-ACM): through Windows' USB serial driver this stream lost
 * whole 512-byte packets, about one in 20,000, while Linux received the same stream whole.
 * The PC side: python/winusb.py.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace usb_stream {

/* the WinUSB device interface GUID the PC finds the board by (python/winusb.py has the same) */
inline constexpr const char *INTERFACE_GUID = "{0781A8D8-EC80-4BA1-ABAA-6C5533217DF0}";

/* starts USB on the High-Speed port. on_rx (optional) gets the bytes the PC sends, in TinyUSB's task. */
bool start(void (*on_rx)(const uint8_t *data, size_t len) = nullptr);

/* the PC has set the board up (enumerated and configured it) */
bool mounted();

/* all of p[0..len), waiting while the PC is not reading; false if the board is no longer mounted. One task. */
bool send(const void *p, size_t len);

} // namespace usb_stream
