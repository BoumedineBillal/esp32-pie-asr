#!/bin/bash
# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0
# run_pc.sh <demo>: a demo's clip test on the PC (Linux): its clips (<demo>/clips/*.wav, make_clips.py) through the
# engine's host build (build/host/transcribe: the board's beam search and commits, the same C), beam 4, with the
# demos' commit at a pause of 0.5 s (PIEASR_PAUSE_MS), without and with the demo's phrase list
# (firmwares/voice_control/phrases/<demo>.txt, if there is one), then the rules (voice_rules_cli <demo>), then
# score.py against <demo>/set.json.
# Output in <demo>/: pc_plain.tsv, pc_phrases.tsv (file, seconds, text), pc_plain_actions.tsv,
# pc_phrases_actions.tsv, and the score.
#   MODEL (default models/kroko-en-q24-v1.bin), LIST (default phrases/<demo>.txt), PAUSE_MS (default 500)
set -e
DEMO=$1
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../../.." && pwd)
D=$HERE/$DEMO
MODEL=${MODEL:-$REPO/models/kroko-en-q24-v1.bin}
LIST=${LIST:-$REPO/firmwares/voice_control/phrases/$DEMO.txt}
export PIEASR_PAUSE_MS=${PAUSE_MS:-500}
cmake -S "$REPO/pieasr/host" -B "$REPO/build/host" > /dev/null
cmake --build "$REPO/build/host" --target transcribe -j > /dev/null
cmake -S "$REPO/components/voice_rules/host" -B "$REPO/build/voice_rules" > /dev/null
cmake --build "$REPO/build/voice_rules" -j > /dev/null
T=$(mktemp -d)
python3 - "$D/clips" "$T" << 'EOF'
import array, os, sys, wave
src, out = sys.argv[1], sys.argv[2]
for f in sorted(os.listdir(src)):
    if f.endswith(".wav"):
        with wave.open(os.path.join(src, f)) as w:
            a = array.array("h", w.readframes(w.getnframes()))
        array.array("f", (v / 32768.0 for v in a)).tofile(open(os.path.join(out, f[:-4] + ".f32"), "wb"))
EOF
ls "$T"/*.f32 | xargs -P 8 -n 10 "$REPO/build/host/transcribe" "$MODEL" int+beam4 2>/dev/null |
    sed "s#$T/##" | sort > "$D/pc_plain.tsv"
"$REPO/build/voice_rules/voice_rules_cli" "$DEMO" < "$D/pc_plain.tsv" > "$D/pc_plain_actions.tsv"
OUT="$D/pc_plain_actions.tsv"
if [ -f "$LIST" ]; then
    ls "$T"/*.f32 | PIEASR_PHRASES=$LIST xargs -P 8 -n 10 "$REPO/build/host/transcribe" "$MODEL" int+beam4 \
        2> "$T/err" | sed "s#$T/##" | sort > "$D/pc_phrases.tsv"
    grep -m1 phrases "$T/err" || true
    "$REPO/build/voice_rules/voice_rules_cli" "$DEMO" < "$D/pc_phrases.tsv" > "$D/pc_phrases_actions.tsv"
    OUT="$OUT $D/pc_phrases_actions.tsv"
fi
rm -rf "$T"
python3 "$HERE/score.py" "$DEMO" $OUT
