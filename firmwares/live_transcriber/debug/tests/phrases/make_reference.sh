#!/bin/bash
# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0
# make_reference.sh: the phrases test's PC reference (Linux): the clips (clips/*.wav) through the engine's host build
# (build/host/transcribe: the board's beam search and commits, the same C) with beam 4, without the phrases and with
# the firmware's default list (phrases/default.txt); the texts are the model's pieces joined (the board's rewrite comes
# after them, in the transcript). Output: reference_beam4.tsv, reference_phrases.tsv.
#   MODEL (default models/kroko-en-q24-v1.bin)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../../.." && pwd)
MODEL=${MODEL:-$REPO/models/kroko-en-q24-v1.bin}
LIST=$REPO/firmwares/live_transcriber/phrases/default.txt
cmake -S "$REPO/pieasr/host" -B "$REPO/build/host" > /dev/null
cmake --build "$REPO/build/host" --target transcribe -j > /dev/null
T=$(mktemp -d)
python3 - "$HERE/clips" "$T" << 'EOF'
import array, os, sys, wave
src, out = sys.argv[1], sys.argv[2]
for f in sorted(os.listdir(src)):
    if f.endswith(".wav"):
        with wave.open(os.path.join(src, f)) as w:
            a = array.array("h", w.readframes(w.getnframes()))
        array.array("f", (v / 32768.0 for v in a)).tofile(open(os.path.join(out, f[:-4] + ".f32"), "wb"))
EOF
ls "$T"/*.f32 | xargs -P 8 -n 10 "$REPO/build/host/transcribe" "$MODEL" int+beam4 2>/dev/null |
    sed "s#$T/##" | sort > "$HERE/reference_beam4.tsv"
ls "$T"/*.f32 | PIEASR_PHRASES=$LIST xargs -P 8 -n 10 "$REPO/build/host/transcribe" "$MODEL" int+beam4 \
    2> "$T/err" | sed "s#$T/##" | sort > "$HERE/reference_phrases.tsv"
grep -m1 phrases "$T/err"
wc -l "$HERE/reference_beam4.tsv" "$HERE/reference_phrases.tsv"
rm -rf "$T"
