// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The recogniser around the engine: the microphone into a ring, the stream of 1.28 s chunks through the encoder and the
 * beam search, the transcript, the phrases; the model from the SD card at boot.
 *
 * asr::run() is the main task (32 KB stack, as the encoder's chunks need). An audio task of its own moves the
 * microphone's samples into the ring and measures their level, so the level keeps moving while a chunk is computed.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace asr {

enum State { WAITING_MODEL, LOADING_MODEL, PREPARING, IDLE, LISTENING, FINISHING, FAILED };

struct Config {
    const char *model_file = nullptr;      /* the weights on the SD card's top folder */
    size_t model_bytes = 0;                /* their size and CRC-32: another file is refused */
    uint32_t model_crc = 0;
    const char *phrases_file = nullptr;    /* the phrase file on the SD card, written with default_phrases if absent */
    const char *default_phrases = nullptr;
    int beam = 4;                          /* hypotheses */
    /* more phrase lists (a demo's own), each read and written like phrases_file; phrases::select() picks the list
     * in use, list 0 being phrases_file's */
    static constexpr int MORE_LISTS = 7;
    const char *more_phrase_files[MORE_LISTS] = {};
    const char *more_default_phrases[MORE_LISTS] = {};
};
void configure(const Config &c);           /* before run() */

void run();                                /* the main task: never returns */
State state();
const char *state_name(State s);
float load_progress();                     /* 0..1 while LOADING_MODEL */
const char *model_source();                /* "the microSD card", "USB", or "" before the weights come */
const char *waiting_reason();              /* while WAITING_MODEL: what is wrong with the card, as a sentence */
const char *failure();                     /* why FAILED */

/* from the screen (taken by the main task at its next turn) */
void request_start();
void request_stop();
void request_clear();
void set_gain(float db);
/* a pause this long commits the search's best hypothesis (pieasr_beam_set_pause: the words before a pause are final
 * at once, not when the search's hypotheses agree or after its 3 s); 0, the default: off. Taken between chunks. */
void set_pause_commit(int ms);
/* fn after each chunk and at a stream's end, on the stream's task (keep it short: a task notification); nullptr: none */
void set_chunk_listener(void (*fn)());

/* the microphone's level now (dBFS, the last 50 ms) and the peak held 1 s */
struct Level {
    float rms_db, peak_db;
    bool clipping;                         /* a sample at full scale in the last second */
};
Level level();

struct Chunk {
    uint16_t fbank_ms, encode_ms, search_ms, total_ms;
    int32_t behind_ms;                     /* the audio received but not processed when it ended */
};
struct Stats {
    int chunks;                            /* this session */
    int32_t listening_ms;                  /* audio received since Start */
    int32_t processed_ms;
    int32_t behind_ms;                     /* received - processed, now */
    int64_t compute_us;                    /* all chunks' compute */
    float x_real_time;                     /* compute / audio processed */
    int32_t mean_ms, median_ms, p95_ms, max_ms;
    uint32_t frames_lost;                  /* the microphone's */
    uint32_t clipped;                      /* samples at full scale */
    uint32_t tokens;
    int64_t uptime_us;
    uint32_t psram_free, psram_min, internal_free;
    float gain_db;
};
Stats stats();
int recent_chunks(Chunk *out, int n);      /* the last n chunks, oldest first; returns how many */

/* the transcript to the next free transcript_NNN.txt on the SD card; out: its name, or why not (main task only) */
bool save_transcript(char *out, size_t cap);
/* the same from the screen: done by the main task between chunks; save_count() grows when it is done, and
 * save_result() then says the file's name or why not (ok: whether it was saved) */
void request_save();
uint32_t save_count();
const char *save_result(bool *ok = nullptr);

/* ---- for the debug link (components/debug); a release build uses none of this ---- */
struct Hooks {
    char *(*next_command)(uint32_t timeout_ms) = nullptr; /* a command line (freed by the service) or nullptr */
    void (*command)(char *line) = nullptr;
    void (*log)(const char *line) = nullptr;               /* each log line */
    void (*audio)(const int16_t *pcm, int n) = nullptr;    /* each microphone block */
    void (*chunk_done)() = nullptr;                        /* after each chunk and at a stream's end */
};
void set_hooks(const Hooks &h);

/* a stream from these calls instead of the microphone (from IDLE) */
bool feed_begin();
bool feed(const int16_t *pcm, int n);      /* false: the ring overran */
bool feed_end();                           /* the rest computed (the stream's true end), back to IDLE */
bool feeding();

/* the weights from elsewhere (a board without the SD card's file): a buffer, then the check and the init */
uint8_t *model_load_begin(size_t n, const char *source);
void model_load_progress(float f);
bool model_load_end(bool received, uint32_t crc);   /* false: not received or another CRC (WAITING_MODEL again) */
bool model_loaded();

/* the last session, for tests */
uint32_t session_crc();                    /* every chunk's encoder output */
int session_frames();
const char *session_pieces();              /* the committed pieces as they came */
const char *session_tokens(size_t *len);   /* " frame:id" for each committed token */
const char *const *vocabulary();           /* the model's pieces, by token id (nullptr before the model) */

} // namespace asr
