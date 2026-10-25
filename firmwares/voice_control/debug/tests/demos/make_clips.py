# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""make_clips.py <voices folder> [demo...]: each demo's clips (texts.py: its commands and near misses), spoken by
every Piper voice of the folder (text to speech), 16 kHz 16-bit wav in <demo>/clips/ (not in git), and
<demo>/set.json: per clip its text, group and expected actions (voice::describe's form). Needs piper-tts and soxr
(pip install piper-tts soxr); the voices from huggingface.co/rhasspy/piper-voices (en_US-lessac-medium,
en_US-ryan-medium, en_GB-alan-medium, en_US-amy-medium).
"""
import json
import os
import sys
import wave

import numpy as np
import soxr
from piper import PiperVoice

from texts import DEMOS

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    voices = sys.argv[1]
    names = sys.argv[2:] or list(DEMOS)
    loaded = [(vn, PiperVoice.load(os.path.join(voices, vn + ".onnx")))
              for vn in sorted(f[:-5] for f in os.listdir(voices) if f.endswith(".onnx"))]
    for demo in names:
        commands, near = DEMOS[demo]
        texts = [("command", t, e) for t, e in commands] + [("near_miss", t, None) for t in near]
        folder = os.path.join(HERE, demo)
        os.makedirs(os.path.join(folder, "clips"), exist_ok=True)
        clips = []
        for vn, voice in loaded:
            for i, (group, text, expect) in enumerate(texts):
                chunks = list(voice.synthesize(text))
                a = np.concatenate([c.audio_float_array for c in chunks]).astype(np.float32)
                a = soxr.resample(a, chunks[0].sample_rate, 16000).astype(np.float32)
                a = np.concatenate([np.zeros(4800, np.float32), a, np.zeros(4800, np.float32)])   # 0.3 s each side
                pcm = np.clip(np.round(a * 32767.0), -32768, 32767).astype("<i2")
                cid = "%s_%02d" % (vn.replace("-", "_"), i)
                with wave.open(os.path.join(folder, "clips", cid + ".wav"), "wb") as w:
                    w.setnchannels(1)
                    w.setsampwidth(2)
                    w.setframerate(16000)
                    w.writeframes(pcm.tobytes())
                clips.append({"id": cid, "voice": vn, "group": group, "text": text, "expect": expect})
        with open(os.path.join(folder, "set.json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump({"clips": clips}, f, indent=1)
        print(demo, len(clips), "clips", flush=True)


if __name__ == "__main__":
    main()
