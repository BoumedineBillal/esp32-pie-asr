// SPDX-FileCopyrightText: 2026 Boumedine Billal
// SPDX-License-Identifier: Apache-2.0

// transcribe <weights> <mode> <audio.f32>...: the text of each raw float32 16 kHz file (features, encoder, search).
// mode: f32 (float products), q8 (int8 products), pie (int8 products in the chip's organization), int (the chip's
// integer configuration) or int<mask> (pieasr_opt_i16 = mask); "+beamN": the beam search (N hypotheses) instead of
// greedy, committed as on the chip; then PIEASR_PHRASES=<phrase file> biases its phrases (all of them, PIEASR_BONUS per
// token, default 2.0) and PIEASR_PAUSE_MS=<ms> commits at a pause that long (pieasr_beam_set_pause). Prints
// "file<TAB>seconds s<TAB>text" per file; the encoder's CRC and more on stderr.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pieasr/pieasr.h"

static void *read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void *p = malloc(*size);
    if (fread(p, 1, *size, f) != *size) {
        exit(1);
    }
    fclose(f);
    return p;
}

static void *alloc(size_t n)
{
    return calloc(1, n);
}

// zlib's CRC-32, chained like crc32(crc, buf, len) (and like the chip's esp_rom_crc32_le)
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

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: transcribe <weights> f32|q8|pie|int[<mask>][+beam<N>] audio.f32...\n");
        return 1;
    }
    size_t bs;
    void *bd = read_all(argv[1], &bs);
    pieasr_model_t *m = alloc(pieasr_model_size());
    char mode[64];
    snprintf(mode, sizeof mode, "%s", argv[2]);
    char *plus = strstr(mode, "+beam");
    const int beam = plus ? atoi(plus + 5) : 0;
    if (plus) {
        *plus = 0;
    }
    pieasr_opt_int = !strncmp(mode, "int", 3);   // "int": the stable configuration; "int<mask>": PIEASR_I16_* mask
    pieasr_opt_i16 = !pieasr_opt_int ? 0 : mode[3] ? atoi(mode + 3) : PIEASR_I16_ALL;
    pieasr_opt_q8 = !strcmp(mode, "pie") || pieasr_opt_int ? 2 : !strcmp(mode, "q8");
    if (pieasr_init(m, bd, bs, alloc)) {
        return 1;
    }
    const char *const *tokens = pieasr_tokens(m);
    pieasr_fbank_t *fb = alloc(pieasr_fbank_size());
    pieasr_fbank_init(fb);
    pieasr_beam_t *bm = NULL;
    if (beam && !(bm = pieasr_beam_new(beam, alloc))) {
        return 1;
    }
    if (beam && getenv("PIEASR_PAUSE_MS")) {   // as the board's demos: a pause commits (40 ms frames)
        pieasr_beam_set_pause(bm, (int)lrint(atof(getenv("PIEASR_PAUSE_MS")) / 40.0));
        fprintf(stderr, "pause commit after %s ms\n", getenv("PIEASR_PAUSE_MS"));
    }
    const char *pf = getenv("PIEASR_PHRASES");
    if (beam && pf) {
        size_t ps;
        char *pt = read_all(pf, &ps);
        pt = realloc(pt, ps + 1);
        pt[ps] = 0;
        static pieasr_phrase_t items[PIEASR_PHRASES_MAX];
        int bad = 0;
        const int ni = pieasr_phrases_parse(pt, items, PIEASR_PHRASES_MAX, &bad);
        const float bonus = getenv("PIEASR_BONUS") ? (float)atof(getenv("PIEASR_BONUS")) : 2.0f;
        pieasr_trie_t *trie = pieasr_trie_new(alloc, free);
        if (!trie) {
            return 1;
        }
        int seqs = 0;
        for (int i = 0; i < ni; i++) {
            for (int f = 0; f < items[i].nforms; f++) {
                const int k = pieasr_trie_add_text(trie, m, items[i].forms[f], bonus);
                if (k < 0) {
                    fprintf(stderr, "phrase \"%s\": a word the vocabulary's pieces cannot spell\n", items[i].forms[f]);
                } else {
                    seqs += k;
                }
            }
        }
        pieasr_trie_build(trie);
        pieasr_beam_set_phrases(bm, trie);
        fprintf(stderr, "phrases: %d items (%d lines not understood), %d sequences, %d trie nodes, bonus %.2f\n", ni,
                bad, seqs, pieasr_trie_nodes(trie), bonus);
        free(pt);
    }
    for (int a = 3; a < argc; a++) {
        size_t as;
        float *audio = read_all(argv[a], &as);
        int n = (int)(as / 4), nf = pieasr_fbank_num_frames(n), nchunks = nf / PIEASR_CHUNK_STEP + 1;
        float *f = malloc(sizeof(float) * (nf + PIEASR_CHUNK_IN) * PIEASR_MEL);
        pieasr_fbank_compute(fb, audio, n, f);
        for (int i = nf * PIEASR_MEL; i < (nf + PIEASR_CHUNK_IN) * PIEASR_MEL; i++) {
            f[i] = -23.025850929940457f;   // log(1e-10): the padding of the last chunk
        }
        fprintf(stderr, "feat crc %s %08x\n", argv[a],
                (unsigned)crc32_update(0, f, sizeof(float) * (nf + PIEASR_CHUNK_IN) * PIEASR_MEL));
        pieasr_reset(m);
        if (beam) {
            pieasr_beam_reset(bm, m);
        }
        static float enc[PIEASR_OUT_T * PIEASR_DIM];
        char text[4096] = "";
        clock_t t0 = clock();
        uint32_t crc = 0;   // CRC-32 of every chunk's encoder output (the chip computes the same)
        for (int c = 0; c < nchunks; c++) {
            pieasr_encode_chunk(m, f + (size_t)c * PIEASR_CHUNK_STEP * PIEASR_MEL, enc);
            crc = crc32_update(crc, enc, sizeof(enc));
            if (beam) {   // as on the chip: the search, then the committed tokens of the settled frames
                pieasr_beam_chunk(bm, m, enc, PIEASR_OUT_T);
                if (c == nchunks - 1) {
                    pieasr_beam_finish(bm);
                }
                int16_t tk[PIEASR_BEAM_OUT];
                int32_t fr[PIEASR_BEAM_OUT];
                const int k = pieasr_beam_take(bm, c == nchunks - 1 ? 1 << 30 : pieasr_beam_settled(bm), tk, fr,
                                               PIEASR_BEAM_OUT);
                for (int i = 0; i < k; i++) {
                    strncat(text, tokens[tk[i]], sizeof(text) - strlen(text) - 1);
                }
                continue;
            }
            int toks[PIEASR_OUT_T];
            pieasr_greedy_chunk(m, enc, PIEASR_OUT_T, toks);
            for (int t = 0; t < PIEASR_OUT_T; t++) {
                int k = toks[t];
                if (k) {
                    strncat(text, tokens[k], sizeof(text) - strlen(text) - 1);
                }
            }
        }
        for (char *p = text; *p; p++) {   // U+2581 -> space
            if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x96 && (unsigned char)p[2] == 0x81) {
                p[0] = ' ';
                memmove(p + 1, p + 3, strlen(p + 3) + 1);
            }
        }
        printf("%s\t%.2f s\t%s\n", argv[a], (double)(clock() - t0) / CLOCKS_PER_SEC, text[0] == ' ' ? text + 1 : text);
        fflush(stdout);
        fprintf(stderr, "enc crc %s %08x\n", argv[a], (unsigned)crc);
        fprintf(stderr, "workspace peak %u floats\n", (unsigned)pieasr_workspace_peak(m));
        free(audio);
        free(f);
    }
    return 0;
}
