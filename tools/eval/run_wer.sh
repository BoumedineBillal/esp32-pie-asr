#!/bin/bash
# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0
# run_wer.sh [set folder] [mode]: the engine's host build on every clip of a set (get_librispeech.py), in parallel,
# then the WER (wer.py). The host build computes what the chip computes, bit for bit.
#   bash tools/eval/run_wer.sh                            data/test-clean, int+beam4 (the firmware's configuration)
#   bash tools/eval/run_wer.sh data/test-clean int        the greedy search
# Environment: MODEL (default models/kroko-en-q24-v1.bin), JOBS (default all cores), NORM (whisper or plain).
# Output: build/wer/<set>_<mode>.tsv, the encoder CRCs in build/wer/<set>_<mode>.err.
set -e
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SET=$(cd "${1:-$REPO/data/test-clean}" && pwd)
MODE=${2:-int+beam4}
MODEL=${MODEL:-$REPO/models/kroko-en-q24-v1.bin}
JOBS=${JOBS:-$(nproc)}
OUT=$REPO/build/wer
cmake -S "$REPO/pieasr/host" -B "$REPO/build/host" > /dev/null
cmake --build "$REPO/build/host" --target transcribe -j "$JOBS" > /dev/null
mkdir -p "$OUT"
NAME=$(basename "$SET")_$MODE
T0=$(date +%s)
ls "$SET"/*.f32 | xargs -P "$JOBS" -n 20 "$REPO/build/host/transcribe" "$MODEL" "$MODE" 2> "$OUT/$NAME.err" |
    sed "s#^$SET/##" | sort > "$OUT/$NAME.tsv"
echo "$(wc -l < "$OUT/$NAME.tsv") clips in $(( $(date +%s) - T0 )) s: $OUT/$NAME.tsv"
python3 "$REPO/tools/eval/wer.py" --refs "$SET/refs.json" --norm "${NORM:-whisper}" "$OUT/$NAME.tsv"
