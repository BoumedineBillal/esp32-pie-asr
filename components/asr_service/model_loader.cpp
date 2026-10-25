// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

/*
 * The model: the weights read from the SD card at boot (the configured file, or any file of their size, checked by
 * its CRC), or given by the debug link; the engine's init; then the stream's buffers and the microphone.
 */
#include <cstdio>
#include <cstring>
#include "esp32p4/rom/cache.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "p4sdcard.hpp"
#include "service_internal.hpp"
#include "soc/soc.h"
#include "transcript.hpp"

namespace asr {

static std::atomic<float> s_progress{0};
static const char *s_source = "";   /* where the weights came from */
static uint8_t *s_blob;
static size_t s_blob_n;
static int64_t s_t0;
static pieasr_model_t *s_m;         /* PSRAM: internal RAM is short, and these are touched a few times a layer */
static pieasr_fbank_t *s_fb;

/* why the model is not there, for the screen */
enum Waiting { NO_CARD, NO_FILE, DAMAGED };
static char s_why[192];

static void set_waiting(Waiting w)
{
    const char *f = g_config.model_file ? g_config.model_file : "the model file";
    switch (w) {
    case NO_CARD:
        snprintf(s_why, sizeof s_why, "The microSD card cannot be read. It must be in the slot and formatted FAT32 (a "
                                      "card of 32 GB or less).");
        break;
    case NO_FILE:
        snprintf(s_why, sizeof s_why, "No model on the microSD card: copy %s to its top folder.", f);
        break;
    default:
        snprintf(s_why, sizeof s_why, "The model file on the microSD card is damaged: copy %s again.", f);
        break;
    }
}

const char *waiting_reason() { return s_why; }
float load_progress() { return s_progress.load(); }
const char *model_source() { return s_source; }
pieasr_model_t *model() { return s_m; }
pieasr_fbank_t *features() { return s_fb; }
bool model_loaded() { return s_blob != nullptr; }
const char *const *vocabulary() { return g_state.load() >= IDLE ? pieasr_tokens(s_m) : nullptr; }

bool model_alloc()
{
    s_m = static_cast<pieasr_model_t *>(heap_caps_calloc(1, pieasr_model_size(), MALLOC_CAP_SPIRAM));
    s_fb = static_cast<pieasr_fbank_t *>(heap_caps_calloc(1, pieasr_fbank_size(), MALLOC_CAP_SPIRAM));
    return s_m && s_fb;
}

/* L1 D-cache and L2 autoload on misses over the PSRAM range (a chunk about 7 % faster: the weights are read in
 * order). These ROM functions are marked as not for applications; their effect is measured and kept. */
void cache_autoload()
{
    struct l1_dcache_l2_autoload_config c = {};
    c.order = CACHE_AUTOLOAD_POSITIVE;
    c.trigger = CACHE_AUTOLOAD_MISS_TRIGGER;
    c.ena0 = 1;
    c.addr0 = SOC_EXTRAM_LOW;
    c.size0 = 32 * 1024 * 1024;
    Cache_Disable_L2_Cache_Autoload();
    Cache_Disable_L1_DCache_Autoload();
    Cache_Config_L2_Cache_Autoload(&c);
    Cache_Enable_L2_Cache_Autoload();
    Cache_Config_L1_DCache_Autoload(&c);
    Cache_Enable_L1_DCache_Autoload();
}

static void *psram_alloc(size_t n) { return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM); }
static uint64_t usec() { return static_cast<uint64_t>(esp_timer_get_time()); }

/* the engine's init on the weights in s_blob, then the stream and the microphone */
static void init_model()
{
    const int64_t t1 = esp_timer_get_time();
    pieasr_opt_ws_floats = 310 * 1024; /* the encoder's peak is 308,448 floats */
    pieasr_opt_inplace = 1;            /* the int8 tiles over their sources in the weights: no copies */
    pieasr_opt_q8 = 2;
    pieasr_opt_int = 1;
    pieasr_opt_i16 = PIEASR_I16_ALL;
    if (const int r = pieasr_init(s_m, s_blob, s_blob_n, psram_alloc)) {
        char why[64];
        snprintf(why, sizeof why, "the engine's init: %d", r);
        fail(why);
        return;
    }
    pieasr_fbank_init(s_fb);
    pieasr_clock = usec;
    transcript::set_vocabulary(pieasr_tokens(s_m), PIEASR_VOCAB + 2);
    phrases_init(s_m);
    /* the stream's buffers after the init: its temporaries (180 KB over what it keeps) have their room then */
    if (!audio_alloc() || !stream_alloc()) {
        fail("no memory for the stream");
        return;
    }
    if (!audio_open()) {
        fail("the microphone did not open");
        return;
    }
    g_state = IDLE;
    log("model ready: init %d ms; PSRAM free %u (largest %u), internal free %u",
        (int)((esp_timer_get_time() - t1) / 1000), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

static bool blob_alloc(size_t n)
{
    s_blob = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, n, MALLOC_CAP_SPIRAM));
    s_blob_n = n;
    if (!s_blob) {
        log("no memory for the weights: %u bytes (PSRAM largest %u)", (unsigned)n,
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        fail("no memory for the model");
    }
    return s_blob != nullptr;
}

/* phrase list `list`'s text: phrases_set_text for the first, phrases_set_list_text for the others */
static void put_phrases(int list, const char *text, const char *source)
{
    if (list == 0)
        phrases_set_text(text, source);
    else
        phrases_set_list_text(list, text, source);
}

/* a phrase file from the card (written with its defaults when it has none) */
static void read_list(p4sdcard::Card &sd, int list, const char *f, const char *def)
{
    def = def ? def : "";
    if (!f) {
        put_phrases(list, def, "built in");
        return;
    }
    const long n = sd.fileSize(f);
    if (n < 0) {
        if (sd.write(f, def, strlen(def))) {
            put_phrases(list, def, "the microSD card (its defaults, written now)");
            log("%s written to the microSD card with the defaults", f);
        } else {
            put_phrases(list, def, "built in");
            log("%s could not be written to the microSD card: the built-in phrases", f);
        }
        return;
    }
    char *t = n <= 16384 ? static_cast<char *>(heap_caps_calloc(1, n + 1, MALLOC_CAP_SPIRAM)) : nullptr;
    if (t && sd.read(f, t, n) == n) {
        put_phrases(list, t, "the microSD card");
    } else {
        heap_caps_free(t);
        put_phrases(list, def, "built in");
        log("%s could not be read (at most 16384 bytes): the built-in phrases", f);
    }
}

/* the phrase files from the card: phrases_file, then the firmware's more lists */
static void read_phrases(p4sdcard::Card &sd)
{
    read_list(sd, 0, g_config.phrases_file, g_config.default_phrases);
    for (int i = 0; i < Config::MORE_LISTS; i++)
        if (g_config.more_phrase_files[i])
            read_list(sd, i + 1, g_config.more_phrase_files[i], g_config.more_default_phrases[i]);
}

/* the files of the card's top folder that may be the weights: the configured name first, then any other file of their
 * size (a browser saves a second download as "name (1).bin") */
struct Candidates {
    static constexpr int MAX = 4;
    char name[MAX][128];
    int n;
    long size;
    const char *first;
};

static void find_candidates(p4sdcard::Card &sd, Candidates &c)
{
    c.n = 0;
    c.size = static_cast<long>(g_config.model_bytes);
    c.first = g_config.model_file ? g_config.model_file : "";
    if (*c.first && sd.fileSize(c.first) == c.size)
        snprintf(c.name[c.n++], sizeof c.name[0], "%s", c.first);
    sd.list(
        [](const char *name, long size, void *arg) {
            Candidates &c = *static_cast<Candidates *>(arg);
            if (size == c.size && c.n < Candidates::MAX && strcmp(name, c.first))
                snprintf(c.name[c.n++], sizeof c.name[0], "%s", name);
        },
        &c);
}

/* read and check the weights from the card; false (WAITING_MODEL, waiting_reason() says why) if the card cannot be
 * read, has no file of the weights' size, or none with their CRC */
static bool read_card()
{
    p4sdcard::Card sd;
    if (!sd.open()) {
        phrases_set_text(g_config.default_phrases ? g_config.default_phrases : "", "built in");
        set_waiting(NO_CARD);
        log("no microSD card, or one that cannot be read (not FAT32)");
        return false;
    }
    read_phrases(sd);
    Candidates c;
    find_candidates(sd, c);
    if (!c.n) {
        set_waiting(NO_FILE);
        log("the microSD card has no file of %u bytes (%s)", (unsigned)g_config.model_bytes, g_config.model_file);
        return false;
    }
    if (!blob_alloc(g_config.model_bytes))
        return false;
    for (int i = 0; i < c.n; i++) {
        g_state = LOADING_MODEL;
        s_progress = 0;
        s_source = "the microSD card";
        const int64_t t0 = esp_timer_get_time();
        uint32_t crc = 0;
        const long r = sd.read(c.name[i], s_blob, g_config.model_bytes, &crc, [](size_t done, size_t total, void *) {
            s_progress = static_cast<float>(done) / total;
        });
        if (r != static_cast<long>(g_config.model_bytes) || crc != g_config.model_crc) {
            log("%s on the microSD card: %ld bytes read, CRC %08x (not %08x)", c.name[i], r, (unsigned)crc,
                (unsigned)g_config.model_crc);
            continue;
        }
        sd.close();
        const int ms = static_cast<int>((esp_timer_get_time() - t0) / 1000);
        s_progress = 1;
        g_state = PREPARING;
        log("model read from the microSD card (%s): %u bytes in %d ms (%.1f MB/s)", c.name[i],
            (unsigned)g_config.model_bytes, ms, g_config.model_bytes / 1e3 / (ms > 0 ? ms : 1));
        return true;
    }
    sd.close();
    heap_caps_free(s_blob);
    s_blob = nullptr;
    s_source = "";
    set_waiting(DAMAGED);
    g_state = WAITING_MODEL;
    return false;
}

void load_from_card()
{
    if (read_card())
        init_model();
}

uint8_t *model_load_begin(size_t n, const char *source)
{
    if (s_blob || !blob_alloc(n))
        return nullptr;
    s_source = source;
    s_progress = 0;
    s_t0 = esp_timer_get_time();
    g_state = LOADING_MODEL;
    return s_blob;
}

void model_load_progress(float f) { s_progress = f; }

bool model_load_end(bool received, uint32_t crc)
{
    const uint32_t c = received ? esp_rom_crc32_le(0, s_blob, s_blob_n) : 0;
    if (!received || c != crc) {
        if (received)
            log("the weights' CRC is %08x, not %08x", (unsigned)c, (unsigned)crc);
        else
            log("the weights did not all come");
        heap_caps_free(s_blob);
        s_blob = nullptr;
        s_source = "";
        g_state = WAITING_MODEL;
        return false;
    }
    s_progress = 1;
    g_state = PREPARING;
    log("weights from %s: %u bytes in %d ms", s_source, (unsigned)s_blob_n,
        (int)((esp_timer_get_time() - s_t0) / 1000));
    init_model();
    return g_state.load() == IDLE;
}

} // namespace asr
