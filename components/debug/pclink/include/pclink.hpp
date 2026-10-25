// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The PC link over USB High-Speed (usb_stream, WinUSB on Windows), for development builds. Messages to the PC: a
 * 4-byte magic, a u32 length, the payload. From the PC: text lines (commands), and after some commands raw bytes.
 *
 * The board talks to the PC only while the PC pings it (a "ping" line every second): usb_stream::send waits while
 * nobody reads, and the board must never wait for a PC that is not there. Replies to a command are always sent.
 *
 * attach() hooks the link into asr_service: the commands run in the main task between chunks, the service's log
 * lines go to the PC (LOG0), each final word too (WRD0), and the microphone's blocks while tapped (AUD0).
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace pclink {

struct Command {
    const char *name;
    int argc;                        /* the words the command takes, its name included */
    void (*run)(int argc, char **argv);
};

/* the link and the service's hooks; app: the name hello replies with */
void attach(const char *app);
/* a firmware's own commands (looked up after the service's) */
void add_commands(const Command *c, int n);

/* the PC pinged in the last 3 s */
bool pc_listening();
/* a message to the PC (any task); dropped when the PC is not listening, unless force */
void send(const char magic[4], const void *a, uint32_t na, const void *b = nullptr, uint32_t nb = 0,
          bool force = false);
/* a text line (TXT0), printf style: the reply to a command (always sent) */
void reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* a log line (LOG0), also printed on the board's console */
void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* receive n raw bytes into dst after replying "ok send"; received() counts them meanwhile */
bool receive(void *dst, size_t n, uint32_t timeout_ms);
size_t received();

/* the service's commands (asr_commands.cpp, model_over_usb.cpp, audio_feed.cpp) */
extern const Command ASR_COMMANDS[];
extern const int ASR_COMMANDS_N;
void send_words(bool from_start);    /* the final words not sent yet (WRD0), or all again */
bool tapping();                      /* the microphone's blocks go to the PC */

} // namespace pclink
