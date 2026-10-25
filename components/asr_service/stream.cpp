// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The stream: the session's chunks from the ring, the encoder and the beam search on each, the transcript's rules on
 * the settled frames, the statistics.
 *
 * Chunk k starts at feature frame f0 = 128 k; its audio slice is one frame before it, to where its last window ends,
 * so its features are the whole stream's, bit for bit. The end of a stream computes the rest with the true end, the
 * tail's features padded with log(0), in nf / 128 + 1 chunks in all.
 */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "service_internal.hpp"
#include "transcript.hpp"

namespace asr {

static constexpr int SLICE = PIEASR_CHUNK_IN * PIEASR_FBANK_SHIFT + 280 + PIEASR_FBANK_SHIFT;
static constexpr float LOG_ZERO = -23.025850929940457f;
static constexpr int CHUNKS_KEPT = 240;

/* the session */
static uint64_t s_base;      /* its first sample (ring index) */
static int64_t s_f0;         /* the first feature frame of the next chunk */
static int s_chunk;          /* chunks run */
static int16_t *s_slice;
static float *s_feats, *s_enc;
static uint32_t s_crc;       /* of every chunk's encoder output */
static pieasr_beam_t *s_kb;
static int s_fed;            /* the frames the transcript has had (the settled ones) */
static int16_t *s_tk;        /* the committed tokens taken, their frames, a block of frames */
static int32_t *s_fr;
static int *s_frames;

/* its record for tests: the committed pieces as they came, its tokens " frame:id" */
static constexpr size_t PIECES_CAP = 4096, TOKENS_CAP = 8192;
static char *s_pieces, *s_tokens;
static size_t s_pieces_len, s_tokens_len;

/* the numbers */
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<int> s_pause_ms{0}; /* set_pause_commit */
static std::atomic<void (*)()> s_listener{nullptr};

void set_pause_commit(int ms) { s_pause_ms = ms; }
void set_chunk_listener(void (*fn)()) { s_listener = fn; }

static void pause_apply() /* 40 ms frames, rounded as the host's transcribe */
{
    pieasr_beam_set_pause(s_kb, static_cast<int>(lrintf(s_pause_ms.load() / 40.0f)));
}
static Chunk s_chunks[CHUNKS_KEPT];
static int s_nchunks;        /* in all this session (s_chunks keeps the last CHUNKS_KEPT) */
static int64_t s_compute_us;
static uint32_t s_tokens_n;
static uint32_t s_psram_min = UINT32_MAX;

uint32_t session_crc() { return s_crc; }
int session_frames() { return s_chunk * PIEASR_OUT_T; }
const char *session_pieces() { return s_pieces ? s_pieces : ""; }
const char *session_tokens(size_t *len)
{
    *len = s_tokens ? s_tokens_len : 0;
    return s_tokens ? s_tokens : "";
}

static void *psram_calloc(size_t n) { return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM); }

bool stream_alloc()
{
    s_slice = static_cast<int16_t *>(heap_caps_malloc(sizeof(int16_t) * SLICE, MALLOC_CAP_SPIRAM));
    s_feats = static_cast<float *>(heap_caps_malloc(sizeof(float) * (PIEASR_CHUNK_IN + 8) * PIEASR_MEL,
                                                    MALLOC_CAP_SPIRAM));
    s_enc = static_cast<float *>(heap_caps_malloc(sizeof(float) * PIEASR_OUT_T * PIEASR_DIM, MALLOC_CAP_SPIRAM));
    s_kb = pieasr_beam_new(g_config.beam, psram_calloc);
    s_tk = static_cast<int16_t *>(psram_calloc(sizeof(int16_t) * PIEASR_BEAM_OUT));
    s_fr = static_cast<int32_t *>(psram_calloc(sizeof(int32_t) * PIEASR_BEAM_OUT));
    s_frames = static_cast<int *>(psram_calloc(sizeof(int) * 64));
    return s_slice && s_feats && s_enc && s_kb && s_tk && s_fr && s_frames;
}

int32_t received_ms() { return static_cast<int32_t>((ring_written() - s_base) * 1000 / RATE); }

/* frames [first, first + n) of the stream (one token or 0 each) into the session's record */
static void record(const int *toks, int first, int n)
{
    if (!s_pieces)
        return;
    const char *const *pieces = pieasr_tokens(model());
    for (int t = 0; t < n; t++) {
        if (toks[t] && pieces[toks[t]]) {
            s_pieces_len += snprintf(s_pieces + s_pieces_len, PIECES_CAP - s_pieces_len, "%s", pieces[toks[t]]);
            s_tokens_len +=
                snprintf(s_tokens + s_tokens_len, TOKENS_CAP - s_tokens_len, " %d:%d", first + t, toks[t]);
        }
        s_pieces_len = std::min(s_pieces_len, PIECES_CAP - 1);
        s_tokens_len = std::min(s_tokens_len, TOKENS_CAP - 1);
    }
}

/* the settled frames (before upto) to the transcript and the record, with the search's committed tokens on them;
 * returns the tokens */
static int feed_settled(int upto)
{
    const int k = pieasr_beam_take(s_kb, upto, s_tk, s_fr, PIEASR_BEAM_OUT);
    int j = 0;
    while (s_fed < upto) {
        const int n = std::min(64, upto - s_fed);
        for (int t = 0; t < n; t++)
            s_frames[t] = 0;
        for (; j < k && s_fr[j] < s_fed + n; j++) /* (a committed token is never before the settled frames) */
            s_frames[std::max(0, static_cast<int>(s_fr[j]) - s_fed)] = s_tk[j];
        record(s_frames, s_fed, n);
        transcript::add_tokens(s_frames, n, received_ms());
        s_fed += n;
    }
    return k;
}

/* one chunk of features (PIEASR_CHUNK_IN frames at x) through the encoder and the search; fbank_us: its features */
static void run_chunk(const float *x, int64_t fbank_us)
{
    phrases_apply(s_kb); /* a change of the phrases, between chunks */
    pause_apply();
    const int64_t t1 = esp_timer_get_time();
    pieasr_encode_chunk(model(), x, s_enc);
    const int64_t t2 = esp_timer_get_time();
    s_crc = esp_rom_crc32_le(s_crc, reinterpret_cast<const uint8_t *>(s_enc),
                             sizeof(float) * PIEASR_OUT_T * PIEASR_DIM);
    pieasr_beam_chunk(s_kb, model(), s_enc, PIEASR_OUT_T);
    const int nt = feed_settled(pieasr_beam_settled(s_kb));
    const int64_t t3 = esp_timer_get_time();
    s_chunk++;
    Chunk c;
    c.fbank_ms = static_cast<uint16_t>(fbank_us / 1000);
    c.encode_ms = static_cast<uint16_t>((t2 - t1) / 1000);
    c.search_ms = static_cast<uint16_t>((t3 - t2) / 1000);
    c.total_ms = static_cast<uint16_t>((fbank_us + t3 - t1) / 1000);
    c.behind_ms = received_ms() - static_cast<int32_t>(s_chunk * PIEASR_CHUNK_STEP * 10);
    portENTER_CRITICAL(&s_stats_lock);
    s_chunks[s_nchunks % CHUNKS_KEPT] = c;
    s_nchunks++;
    s_compute_us += fbank_us + t3 - t1;
    s_tokens_n += nt;
    portEXIT_CRITICAL(&s_stats_lock);
    if (g_hooks.chunk_done)
        g_hooks.chunk_done();
    if (auto fn = s_listener.load())
        fn();
}

void session_start()
{
    pieasr_reset(model());
    pieasr_beam_reset(s_kb, model());
    phrases_apply(s_kb);
    pause_apply();
    s_fed = 0;
    s_base = ring_written();
    s_f0 = 0;
    s_chunk = 0;
    s_crc = 0;
    if (!s_pieces) {
        s_pieces = static_cast<char *>(heap_caps_malloc(PIECES_CAP, MALLOC_CAP_SPIRAM));
        s_tokens = static_cast<char *>(heap_caps_malloc(TOKENS_CAP, MALLOC_CAP_SPIRAM));
        if (!s_tokens) {
            heap_caps_free(s_pieces);
            s_pieces = nullptr;
        }
    }
    s_pieces_len = s_tokens_len = 0;
    if (s_pieces)
        s_pieces[0] = s_tokens[0] = 0;
    portENTER_CRITICAL(&s_stats_lock);
    s_nchunks = 0;
    s_compute_us = 0;
    s_tokens_n = 0;
    portEXIT_CRITICAL(&s_stats_lock);
}

bool run_ready_chunks()
{
    for (;;) {
        const int64_t b = s_f0 ? PIEASR_FBANK_SHIFT * (s_f0 - 1) : 0;                   /* one frame before */
        const int64_t e = PIEASR_FBANK_SHIFT * (s_f0 + PIEASR_CHUNK_IN - 1) + 280;       /* its last window's end */
        if (static_cast<int64_t>(ring_written() - s_base) < e)
            return true;
        const int64_t t0 = esp_timer_get_time();
        if (!ring_copy(s_slice, s_base + b, static_cast<int>(e - b)))
            return false;
        pieasr_fbank_compute_pcm16(features(), s_slice, static_cast<int>(e - b), s_feats);
        const int64_t fb_us = esp_timer_get_time() - t0;
        run_chunk(s_feats + (s_f0 ? PIEASR_MEL : 0), fb_us);
        s_f0 += PIEASR_CHUNK_STEP;
    }
}

bool finish_stream()
{
    const int64_t n = ring_written() - s_base, b = s_f0 ? PIEASR_FBANK_SHIFT * (s_f0 - 1) : 0;
    const int nf = pieasr_fbank_num_frames(static_cast<int>(n)), nchunks = nf / PIEASR_CHUNK_STEP + 1;
    const int j0 = s_f0 ? 1 : 0;
    const int rf = n > b ? pieasr_fbank_num_frames(static_cast<int>(n - b)) : 0;
    const int64_t total = j0 + (static_cast<int64_t>(nchunks) * PIEASR_CHUNK_STEP - s_f0) + PIEASR_CHUNK_IN + 8;
    float *rest = static_cast<float *>(heap_caps_malloc(sizeof(float) * total * PIEASR_MEL, MALLOC_CAP_SPIRAM));
    int16_t *tail = n > b ? static_cast<int16_t *>(heap_caps_malloc(sizeof(int16_t) * (n - b), MALLOC_CAP_SPIRAM))
                          : nullptr;
    bool ok = rest && (n <= b || tail);
    if (ok) {
        const int64_t t0 = esp_timer_get_time();
        for (int64_t i = 0; i < total * PIEASR_MEL; i++)
            rest[i] = LOG_ZERO;
        if (rf > 0) {
            ok = ring_copy(tail, s_base + b, static_cast<int>(n - b));
            if (ok)
                pieasr_fbank_compute_pcm16(features(), tail, static_cast<int>(n - b), rest);
        }
        for (int64_t i = static_cast<int64_t>(j0 + nf - s_f0) * PIEASR_MEL; i < total * PIEASR_MEL; i++)
            rest[i] = LOG_ZERO;
        int64_t fb_us = esp_timer_get_time() - t0;
        while (ok && s_chunk < nchunks) {
            run_chunk(rest + (static_cast<int64_t>(j0) + static_cast<int64_t>(s_chunk) * PIEASR_CHUNK_STEP - s_f0) *
                                 PIEASR_MEL,
                      fb_us);
            fb_us = 0;
        }
    }
    heap_caps_free(rest);
    heap_caps_free(tail);
    pieasr_beam_finish(s_kb); /* the best hypothesis' open tokens */
    const int nt = feed_settled(pieasr_beam_frame(s_kb));
    portENTER_CRITICAL(&s_stats_lock);
    s_tokens_n += nt;
    portEXIT_CRITICAL(&s_stats_lock);
    transcript::finish(received_ms());
    if (g_hooks.chunk_done)
        g_hooks.chunk_done();
    if (auto fn = s_listener.load())
        fn();
    return ok;
}

Stats stats()
{
    Stats s = {};
    static int32_t sorted[CHUNKS_KEPT];
    portENTER_CRITICAL(&s_stats_lock);
    s.chunks = s_nchunks;
    s.compute_us = s_compute_us;
    s.tokens = s_tokens_n;
    const int n = std::min(s_nchunks, CHUNKS_KEPT);
    for (int i = 0; i < n; i++)
        sorted[i] = s_chunks[i].total_ms;
    portEXIT_CRITICAL(&s_stats_lock);
    const State st = g_state.load();
    const bool live = st == LISTENING || st == FINISHING;
    s.listening_ms = live || s.chunks ? received_ms() : 0;
    s.processed_ms = static_cast<int32_t>(s.chunks * PIEASR_CHUNK_STEP * 10);
    s.behind_ms = live ? std::max<int32_t>(0, s.listening_ms - s.processed_ms) : 0;
    s.x_real_time = s.processed_ms ? static_cast<float>(s.compute_us / 1000.0 / s.processed_ms) : 0;
    if (n) {
        std::sort(sorted, sorted + n);
        int64_t sum = 0;
        for (int i = 0; i < n; i++)
            sum += sorted[i];
        s.mean_ms = static_cast<int32_t>(sum / n);
        s.median_ms = sorted[n / 2];
        s.p95_ms = sorted[std::min(n - 1, n * 95 / 100)];
        s.max_ms = sorted[n - 1];
    }
    s.frames_lost = frames_lost();
    s.clipped = clipped();
    s.uptime_us = esp_timer_get_time();
    s.psram_free = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    s_psram_min = std::min(s_psram_min, s.psram_free);
    s.psram_min = s_psram_min;
    s.internal_free = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    s.gain_db = gain_now();
    return s;
}

int recent_chunks(Chunk *out, int n)
{
    portENTER_CRITICAL(&s_stats_lock);
    const int have = std::min(s_nchunks, CHUNKS_KEPT), k = std::min(n, have);
    for (int i = 0; i < k; i++)
        out[i] = s_chunks[(s_nchunks - k + i) % CHUNKS_KEPT];
    portEXIT_CRITICAL(&s_stats_lock);
    return k;
}

} // namespace asr
