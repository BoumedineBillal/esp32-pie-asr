# Design

How a 65.7M-parameter streaming transducer runs faster than real time on a microcontroller, and how the chip is
kept bit-exact with a PC build so that every accuracy number can be measured on the PC.

## The pipeline

| step | where | time per 1.28 s chunk |
|---|---|---|
| 16 kHz audio from the ES8311 microphone into a 4 s ring | an audio task | |
| 80 log-mel features every 10 ms (Kaldi-compatible) | `pieasr/src/features` | 14.0 ms |
| the encoder: 141 feature frames in (128 new), 32 encoder frames out | `pieasr/src/encoder` | 742.2 ms |
| the beam search, 4 hypotheses, phrase biasing, commits | `pieasr/src/search` | 138.1 ms |
| words and lines, the phrases' rewrites, the screen | `components/asr_service`, the app | |
| **total** | | **895.8 ms (0.70× real time)** |

Means over the 8 test clips on the ESP32-P4X at 400 MHz, with the screen drawing on core 1 meanwhile.

Chunk *k* starts at feature frame 128 *k*; its features are computed from its own slice of the audio, so they are the
whole stream's features bit for bit. The end of a stream computes the rest with the true end.

## The model

Kroko is icefall's Zipformer2 transducer in its "M" configuration: six encoder stacks at 50, 25, 12.5, 6.25, 12.5
and 25 frames per second (widths 192 to 512, 16 layers), a stateless decoder (the last 2 tokens), a joiner and 650
BPE tokens; 65.7M parameters. Each stack keeps 5.12 s of attention context and its convolution caches between
chunks, with no lookahead: the memory is the same however long the stream.

## The weights: 2 and 4 bits

Every large matrix of the encoder is stored in the [Cactus Quants](https://docs.cactuscompute.com/latest/docs/cactus_quants/)
layout: each row in groups of 128 weights (16 to 64 for the few widths that are not multiples of 128), each group
rotated by a Hadamard matrix, one norm per group, and 2 or 4 bits per weight indexing the Lloyd-Max levels of a
Gaussian. The bits were chosen per matrix by its sensitivity (the encoder output's error when only that matrix is
rounded), and the rounding compensates its own error on unlabeled calibration audio. No training.

| | parameters |
|---|---|
| 4 bits (193 of the 290 encoder matrices) | 25.4M |
| 2 bits (the rest; most of the large middle stack) | 38.3M |
| biases, norms, depthwise convolutions, gates (float16) | 1.1M |
| decoder and joiner (int8, as released) | 0.9M |

The file, 26.65 MB, is used in place: `pieasr_init` reads the tensors where they are, and writes the few int8 copies
it needs over their float16 sources.

## The products

A row's weights are the levels of its codes, scaled by the group's norm and rotated back:

    W[o, g] = (LV[code] / sqrt(G) * norm[o, g]) @ H_G
    y[o]    = sum_g norm[o, g] / G * sum_k LV[code[o, g, k]] * (Hu x_g)[k]

so the input is rotated once per group (`Hu`: the unnormalized Hadamard, additions only) and that rotation serves
every row. On the chip:

1. each input group is rotated in int32 (exact), scaled to int8 by a power of 2 (the shift is free);
2. the codes are decoded to int8 levels through a table: `pq_decode2`, 851 cycles per 2048 weights;
3. a tile kernel multiplies 16 rows by the group: `pq_tile16`, 168 cycles per frame and tile of 16 × 128, 12.1 MACs
   per cycle;
4. the int32 sums are scaled by the group norms into int16.

The kernels are PIE SIMD assembly (`pieasr/src/kernels/pie`), each next to its C reference (`kernels/c`). The two
cores share each product: the input frames are quantized half on each core, then the tiles are split by blocks of
output rows. The dense layers (the front end's convolutions, the joiner) use the same tile kernel with int8 weights.

## Integer arithmetic

Between the products the activations are int16 with static scales; into the products, int8. The nonlinearities
(SwooshL, SwooshR, sigmoid, tanh, and the attention's exp) are tables indexed by the int16 input; BiasNorm sums its
squares in integers (`pq_sumsq16`) and applies its factor with the bypass in one kernel.
The attention caches are int8, the convolution caches int16.

This costs no accuracy. On LibriSpeech test-clean, greedy search: 4.53 % with the 2/4-bit weights and float
activations (a PyTorch simulation), 4.52 % on the chip.

## Bit-exact with the PC

The engine's host build (`pieasr/host`) compiles the same C with the kernels' C references. Three rules make its
results the chip's, bit for bit:

- every function of `math/detmath.h` is built from IEEE `+ - * /` only, because libm's `exp`, `log`, `tanh` differ
  in the last bit between glibc and newlib, and the features and tables would differ;
- both sides compile with `-ffp-contract=off` (no fused multiply-adds);
- each PIE kernel has a C reference, and each two-core part writes only its own outputs.

Checked on the board: every kernel against its reference (`pieasr/tests/target`, 22 checks, 0 values wrong); the
encoder's output CRC over every chunk and the final text on 8 LibriSpeech clips (`stream_check`); the search's
tokens on 140 phrase-biasing clips (`phrases_check`). So the WERs measured with the host build on all of
test-clean and test-other are the chip's.

## Memory

| PSRAM (32 MB) | |
|---|---|
| the weights, in place | 26.65 MB |
| the engine's state and workspace | about 4 MB |
| the screen's frame buffer (1024 × 600 RGB565) | 1.2 MB |
| left while listening | about 0.5 MB at the lowest |

Internal RAM holds the hot code (the products' drivers, the attention and first-convolution loops, the kernels and
the two-core runtime, `pieasr/linker/pieasr.lf`) and the 256 KB L2 cache, set to 128-byte lines with autoload on
misses (a chunk about 7 % faster: the weights are read in order). The release build leaves 121 KB of internal RAM
free (voice_control's, 104 KB); the debug link (USB) takes 47.8 KB more.

## The search and the transcript

The beam search is k2's modified beam search: at most one token per encoder frame, hypotheses with the same tokens
merged, the best one length-normalized at the end. Each hypothesis' best tokens are exact within it, so one
hypothesis gives the greedy search's tokens bit for bit. Its scores use the deterministic `exp` and `log`.

A token is committed when every hypothesis agrees on it; a token still open after 75 frames (3 s) is committed from
the best hypothesis and the hypotheses that disagree are dropped. The transcript's rules then make words and lines:
a word is final when the next word starts or after 0.5 s without a token; a pause of 1.5 s starts a new line.

On the voice-control pages a pause also commits: once the best hypothesis has had no token for 0.5 s, it is committed
whole and the others are dropped (`pieasr_beam_set_pause`, off by default). Without it, an order's last words waited
until the hypotheses agreed, and a twin that differs only in case ("C" for "c") could keep them apart until the 3 s
limit: from the end of an order to its action took 3.1 s (median), 1.9 s with it. The transcriber's pages keep the
default.

**Phrases.** The list's words and sentences become token sequences in a trie (Aho-Corasick): each word split every
way the model's pieces allow, the fewest-piece splits kept, the first word also capitalized. A hypothesis that
follows a phrase gets a bonus of 2.0 per token, loses it if the phrase breaks off, and keeps it once the phrase is
complete. A phrase's spoken forms (`ESP thirty two`) are rewritten in the transcript to its shown form (`ESP32`).

## Voice control

`firmwares/voice_control` keeps the transcriber and adds six demos in which spoken orders drive a device simulated
on the screen: a robot vacuum, a smart home, a car cabin, a factory line, a kitchen, a presenter. There is no
language model. Each demo is a table of rules (`components/voice_rules`, plain C++ with its tests on the PC), and its
page shows which words fired which action.

**Utterances.** While a demo's page is open, the transcript's final words make an utterance when the speaker pauses
(no word being written) or at 40 words. The model can stay quiet for longer than the pause inside a word or before
one ("Stop" at encoder frame 18, "everything" at 32, 0.56 s later), so the rest of a word that the time-out cut is
joined back to it, and an utterance that does nothing on its own is taken again with the words that follow within
2 s.

**Rules.** An utterance is split into parts at "and", "then", "but" and punctuation. In each part an action fires
when one of its verbs is followed by one of its targets; a part without a verb takes the previous part's ("clean the
kitchen and the bedroom"). The verb must open the order (after "please", "can you", "I want you to"), so "I cleaned
the kitchen yesterday" does nothing. "not", "except" and "without" cancel the target after them; a negation before
the verb ("don't") cancels the part. A demo can also read numbers as the model writes them ("twenty two", "one
hundred and eighty"), places said anywhere in the part ("the kitchen lights", "the lights in the kitchen") and split
verbs ("turn the lights off", "lights off"). Commands said alone ("stop", "next slide") fire only at an order's
start, and some only as the whole part: "next slide please" moves the slide, "next slide shows our results" does not.

**Worlds.** Each demo's actions drive a simulation that its page draws: the vacuum cleans rooms in lanes and goes
back to its dock, boxes move past the factory's three stations, the oven heats, the timers count down. The
simulations are deterministic and tested on the PC.

**Phrase lists.** Each demo has its own list (10 words, 10 sentences), in use while its pages are open. The lists
were chosen on the clips below. A sentence that begins longer orders can hurt: "turn on the burner" pulled "turn on
the back left burner" to itself, without its place, and was taken out.

**Measured.** Each demo has 188 clips: 25 orders and 22 near misses (the demo's words in ordinary sentences, "the
press conference starts at noon"), each spoken by 4 text-to-speech voices (Piper). The engine's PC build gives the
text and the rules give the actions; the board gives the same actions as the PC on every clip.

| | actions | orders exact, no list | with its list | false actions (88 near misses) |
|---|---|---|---|---|
| robot vacuum | 9 | 92 % | 96 % | 0 |
| smart home | 18 | 93 % | 97 % | 0 |
| car cabin | 21 | 93 % | 97 % | 0 |
| factory line | 9 | 86 % | 94 % | 0 |
| kitchen | 16 | 97 % | 98 % | 0 |
| presenter | 14 | 97 % | 99 % | 0 |

Every miss is a word heard wrong ("Stop" as "top", "Hoover" as "over", "Dim" as "Then"): every order heard as it
was said gave its action.

From the end of an order to its action, with the orders fed at the speed of real time (20 per demo): median 1.84 s
and at most 2.27 s on the vacuum's page, median 1.97 s and at most 2.57 s on the factory's. An order ends with a
pause of 0.5 s; its last chunk (up to 1.28 s of audio) must then come in and be computed (0.9 s).

**The screen.** A demo's page draws what moves (the robot, the belt, a slide coming in) in its own box, 10 frames a
second, above the engine's worker; the whole world only when its state changes. This costs compute time: with the
factory's belt moving, at most 0.81× real time over 3 minutes, against 0.70× on the transcriber.

## The application

The firmware, `firmwares/voice_control`, opens on a home page: live transcription, then the voice-control demos.
`firmwares/live_transcriber` is the transcriber alone; the transcriber's numbers in this document were measured with
it.

`components/asr_service` runs the stream in the main task (32 KB stack) between the commands of the screen; an audio
task keeps the microphone's level moving while a chunk is computed. The screen task runs on core 1 below the engine's
worker, in core 1's idle time (a demo's moving parts above it). The release build reads the weights and the phrase lists
from the microSD card; the debug build adds a link over USB High-Speed to a PC (the model from the PC when the card
has none, a stream fed from the PC, the words, the log, screenshots, touch), which the board tests use. The engine's
and the transcriber's, in `firmwares/live_transcriber/debug/tests`:

| test | checks |
|---|---|
| `stream_check` | the 8 clips through the live path: encoder CRCs and texts = the PC's |
| `words_check` | the words and lines against the rules written again in Python, 11 streams |
| `phrases_check` | 140 clips with the phrases on: the board's tokens and words = the PC's |
| `pc_check` | the PC tool, saving to the SD card, the settings across a reboot, the boot |
| `shots`, `compare_shots` | every screen in both themes, no text cut, the same pixels as the references |
| `live_check` | the microphone live for minutes: real time, nothing lost, constant memory |

voice_control's tests (`firmwares/voice_control/debug/tests`):

| test | checks |
|---|---|
| `demo_check` | each demo: its rules' host test said to the board, and its 188 clips: the board's actions = the PC's |
| `latency_check` | from the end of an order to its action, on the vacuum's and the factory's pages |
| `shots` | every page, no text cut |
| `live_check` | the microphone live on the factory's page, its belt moving: real time, nothing lost, constant memory |
