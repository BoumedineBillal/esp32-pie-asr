// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * What the service's parts give each other (asr_service.hpp is what the rest of the firmware sees).
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "asr_service.hpp"
#include "pieasr/pieasr.h"

namespace asr {

constexpr int RATE = 16000;            /* the engine's sample rate */

extern Config g_config;
extern Hooks g_hooks;
extern std::atomic<State> g_state;

/* service.cpp */
void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   /* the console, and the log hook */
void fail(const char *why);            /* FAILED, with the reason */

/* model_loader.cpp: the weights, the engine's init */
bool model_alloc();                    /* the engine's state structs, in PSRAM (at the main task's start) */
void cache_autoload();                 /* the caches' autoload over PSRAM */
void load_from_card();                 /* at boot: read, check, init; WAITING_MODEL still if the card has no model */
pieasr_model_t *model();
pieasr_fbank_t *features();

/* audio_ring.cpp: the microphone and the ring */
constexpr int RING = 1 << 16;          /* samples: 4.1 s */
bool audio_alloc();                    /* the ring (after the engine's init: its temporaries come first) */
bool audio_open();                     /* the microphone and its task */
uint64_t ring_written();               /* samples written since boot */
void ring_push(const int16_t *x, int n);
bool ring_copy(int16_t *dst, uint64_t from, int n);   /* false if the ring has already dropped them */
void set_feeding(bool on);             /* feed() fills the ring: the microphone's samples not */
uint32_t frames_lost();
uint32_t clipped();
float gain_now();

/* stream.cpp: the session, the chunks, the numbers */
bool stream_alloc();                   /* the slice, features, encoder output and the search (after the init) */
void session_start();                  /* a new stream from the ring's present */
bool run_ready_chunks();               /* every chunk whose audio is in; false if the ring dropped audio needed */
bool finish_stream();                  /* the rest with the true end */
int32_t received_ms();                 /* the session's audio so far */

/* phrases.cpp */
void phrases_set_text(const char *text, const char *source);   /* the file (at boot), before phrases_init */
void phrases_set_list_text(int list, const char *text, const char *source);   /* the same for list 1, 2, ... */
void phrases_init(const pieasr_model_t *m);
void phrases_apply(pieasr_beam_t *b);  /* between chunks: the trie and the rewrites again if anything changed */

} // namespace asr
