// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// test_golden <weights> <data dir>: the clips of <data dir>/golden.json through the chip's configuration (the integer
// path, int16 activations): each clip's encoder CRC (zlib's CRC-32 chained over the chunks' outputs, as the chip
// computes it), its greedy text and its beam-4 text against the recorded ones.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pieasr/pieasr.h"

#define MAXCLIPS 32

typedef struct {
    char id[64], crc[16], greedy[1024], beam4[1024];
} clip_t;

static char *read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *p = aligned_alloc(64, (*size + 64) / 64 * 64);
    if (!p || fread(p, 1, *size, f) != *size) {
        fclose(f);
        free(p);
        return NULL;
    }
    fclose(f);
    p[*size] = 0;
    return p;
}

static void *zalloc(size_t n)
{
    return calloc(1, n);
}

static uint32_t crc32_update(uint32_t crc, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

// ---- golden.json: { "<id>": { "crc": "...", "greedy": "...", "beam4": "..." }, ... } (strings without escapes but
// \" and \\)
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    return p;
}

static const char *json_string(const char *p, char *out, size_t cap)
{
    p = skip_ws(p);
    if (*p != '"') {
        return NULL;
    }
    size_t n = 0;
    for (p++; *p && *p != '"'; p++) {
        if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) {
            p++;
        }
        if (n + 1 < cap) {
            out[n++] = *p;
        }
    }
    out[n] = 0;
    return *p == '"' ? p + 1 : NULL;
}

static int parse_golden(const char *p, clip_t *clips, int max)
{
    int n = 0;
    p = skip_ws(p);
    if (*p++ != '{') {
        return -1;
    }
    for (;;) {
        p = skip_ws(p);
        if (*p == '}') {
            return n;
        }
        if (n == max || !(p = json_string(p, clips[n].id, sizeof clips[n].id))) {
            return -1;
        }
        p = skip_ws(p);
        if (*p++ != ':' || *(p = skip_ws(p)) != '{') {
            return -1;
        }
        p++;
        for (;;) {
            char key[32];
            p = skip_ws(p);
            if (*p == '}') {
                p++;
                break;
            }
            if (!(p = json_string(p, key, sizeof key)) || *(p = skip_ws(p)) != ':') {
                return -1;
            }
            p++;
            char *dst = !strcmp(key, "crc") ? clips[n].crc : !strcmp(key, "greedy") ? clips[n].greedy : clips[n].beam4;
            const size_t cap = !strcmp(key, "crc") ? sizeof clips[n].crc : sizeof clips[n].greedy;
            if (!(p = json_string(p, dst, cap))) {
                return -1;
            }
            p = skip_ws(p);
            if (*p == ',') {
                p++;
            }
        }
        n++;
        p = skip_ws(p);
        if (*p == ',') {
            p++;
        }
    }
}

// the samples of a 16-bit mono PCM wav (its "data" chunk)
static int16_t *read_wav(const char *path, int *n)
{
    size_t size;
    char *d = read_all(path, &size);
    if (!d || size < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) {
        free(d);
        return NULL;
    }
    for (size_t p = 12; p + 8 <= size;) {
        uint32_t len;
        memcpy(&len, d + p + 4, 4);
        if (!memcmp(d + p, "data", 4) && p + 8 + len <= size) {
            int16_t *s = malloc(len + 2);
            memcpy(s, d + p + 8, len);
            *n = (int)(len / 2);
            free(d);
            return s;
        }
        p += 8 + len + (len & 1);
    }
    free(d);
    return NULL;
}

// the pieces as text: U+2581 -> space, the leading space removed
static void to_text(char *text)
{
    for (char *p = text; *p; p++) {
        if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x96 && (unsigned char)p[2] == 0x81) {
            p[0] = ' ';
            memmove(p + 1, p + 3, strlen(p + 3) + 1);
        }
    }
    if (text[0] == ' ') {
        memmove(text, text + 1, strlen(text));
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: test_golden <weights> <data dir>\n");
        return 2;
    }
    char path[1024];
    size_t size;
    snprintf(path, sizeof path, "%s/golden.json", argv[2]);
    char *json = read_all(path, &size);
    static clip_t clips[MAXCLIPS];
    const int nclips = json ? parse_golden(json, clips, MAXCLIPS) : -1;
    if (nclips <= 0) {
        printf("FAIL: %s not read\n", path);
        return 1;
    }
    char *weights = read_all(argv[1], &size);
    if (!weights) {
        printf("FAIL: %s not read\n", argv[1]);
        return 1;
    }
    pieasr_opt_int = 1;
    pieasr_opt_i16 = PIEASR_I16_ALL;
    pieasr_opt_q8 = 2;
    pieasr_model_t *m = zalloc(pieasr_model_size());
    pieasr_fbank_t *fb = zalloc(pieasr_fbank_size());
    pieasr_beam_t *b = pieasr_beam_new(4, zalloc);
    if (!m || !fb || !b || pieasr_init(m, weights, size, zalloc)) {
        printf("FAIL: init\n");
        return 1;
    }
    pieasr_fbank_init(fb);
    const char *const *tokens = pieasr_tokens(m);
    int fails = 0;
    for (int c = 0; c < nclips; c++) {
        int n = 0;
        int16_t *pcm = snprintf(path, sizeof path, "%s/%s.wav", argv[2], clips[c].id) < (int)sizeof path
                           ? read_wav(path, &n)
                           : NULL;
        if (!pcm) {
            printf("FAIL %s: not read\n", path);
            fails++;
            continue;
        }
        const int nf = pieasr_fbank_num_frames(n), nchunks = nf / PIEASR_CHUNK_STEP + 1;
        float *x = malloc(sizeof(float) * (size_t)(nf + PIEASR_CHUNK_IN) * PIEASR_MEL);
        pieasr_fbank_compute_pcm16(fb, pcm, n, x);
        for (int i = nf * PIEASR_MEL; i < (nf + PIEASR_CHUNK_IN) * PIEASR_MEL; i++) {
            x[i] = -23.025850929940457f;
        }
        pieasr_reset(m);
        pieasr_beam_reset(b, m);
        static float enc[PIEASR_OUT_T * PIEASR_DIM];
        static char greedy[4096], beam[4096];
        greedy[0] = beam[0] = 0;
        uint32_t crc = 0;
        for (int k = 0; k < nchunks; k++) {
            pieasr_encode_chunk(m, x + (size_t)k * PIEASR_CHUNK_STEP * PIEASR_MEL, enc);
            crc = crc32_update(crc, enc, sizeof(enc));
            int toks[PIEASR_OUT_T];
            pieasr_greedy_chunk(m, enc, PIEASR_OUT_T, toks);
            for (int t = 0; t < PIEASR_OUT_T; t++) {
                if (toks[t]) {
                    strncat(greedy, tokens[toks[t]], sizeof(greedy) - strlen(greedy) - 1);
                }
            }
            pieasr_beam_chunk(b, m, enc, PIEASR_OUT_T);
            if (k == nchunks - 1) {
                pieasr_beam_finish(b);
            }
            int16_t tk[PIEASR_BEAM_OUT];
            int32_t fr[PIEASR_BEAM_OUT];
            const int nt = pieasr_beam_take(b, k == nchunks - 1 ? 1 << 30 : pieasr_beam_settled(b), tk, fr,
                                            PIEASR_BEAM_OUT);
            for (int i = 0; i < nt; i++) {
                strncat(beam, tokens[tk[i]], sizeof(beam) - strlen(beam) - 1);
            }
        }
        to_text(greedy);
        to_text(beam);
        char crcs[16];
        snprintf(crcs, sizeof crcs, "%08x", (unsigned)crc);
        const int ok_c = !strcmp(crcs, clips[c].crc), ok_g = !strcmp(greedy, clips[c].greedy),
                  ok_b = !strcmp(beam, clips[c].beam4);
        fails += !ok_c + !ok_g + !ok_b;
        printf("%s %s: encoder crc %s %s, greedy %s, beam 4 %s\n", ok_c && ok_g && ok_b ? "PASS" : "FAIL",
               clips[c].id, crcs, ok_c ? "same" : "DIFFERS", ok_g ? "same" : "DIFFERS", ok_b ? "same" : "DIFFERS");
        if (!ok_c) {
            printf("    want crc %s\n", clips[c].crc);
        }
        if (!ok_g) {
            printf("    greedy: %s\n    want:   %s\n", greedy, clips[c].greedy);
        }
        if (!ok_b) {
            printf("    beam 4: %s\n    want:   %s\n", beam, clips[c].beam4);
        }
        free(pcm);
        free(x);
    }
    printf("%s: %d clips, %d differences\n", fails ? "FAIL" : "PASS", nclips, fails);
    return fails != 0;
}
