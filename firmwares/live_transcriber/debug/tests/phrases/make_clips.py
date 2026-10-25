# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""make_clips.py: the phrases test's clips: 35 sentences with the default phrase list's words and sentences
(phrases/default.txt) and 10 without any, with near-miss words, each spoken by 4 Piper voices (text to speech), 16 kHz
16-bit wav in clips/ (not in git), and set.json: per clip its written text, group and phrases. Needs piper-tts and
soxr (pip install piper-tts soxr); the voices from huggingface.co/rhasspy/piper-voices:

    python make_clips.py <voices folder>     (en_US-lessac-medium, en_US-ryan-medium, en_GB-alan-medium, en_US-amy-medium)
"""
import json
import os
import sys
import wave

import numpy as np
import soxr
from piper import PiperVoice

HERE = os.path.dirname(os.path.abspath(__file__))
WORDS = ["ESP32-P4", "ESP32", "Espressif", "Kroko"]
SENTENCES = ["turn on the kitchen light", "turn on the light", "turn off the light", "start recording",
             "stop recording", "next slide", "close the bedroom window", "play the next song",
             "set the temperature to 20 degrees", "what is the weather tomorrow"]
SPOKEN = [("ESP32-P4", "E S P thirty two P four"), ("ESP32", "E S P thirty two"), ("20 degrees", "twenty degrees")]
TEXTS = [
    ("word", "The ESP32 is a cheap microcontroller."),
    ("word", "Espressif makes the ESP32 family of chips."),
    ("word", "Kroko runs in real time on the ESP32-P4."),
    ("word", "I bought an ESP32-P4 board from Espressif."),
    ("word", "Can the ESP32 run Kroko without a network?"),
    ("word", "Espressif ships the ESP32-P4 with a camera interface."),
    ("word", "Kroko is a streaming speech recognizer."),
    ("word", "We flashed the firmware onto the ESP32."),
    ("word", "The new ESP32-P4 board arrived today."),
    ("word", "Espressif sells the chips in large numbers."),
    ("sentence", "Turn on the light."),
    ("sentence", "Please turn off the light."),
    ("sentence", "Start recording."),
    ("sentence", "Stop recording now."),
    ("sentence", "Next slide."),
    ("sentence", "Turn on the kitchen light."),
    ("sentence", "Close the bedroom window."),
    ("sentence", "Play the next song."),
    ("sentence", "Set the temperature to 20 degrees."),
    ("sentence", "What is the weather tomorrow?"),
    ("sentence", "It is dark, turn on the kitchen light."),
    ("sentence", "When the meeting ends, stop recording."),
    ("sentence", "Please close the bedroom window and turn off the light."),
    ("sentence", "Play the next song and turn on the light."),
    ("sentence", "Set the temperature to 20 degrees and close the bedroom window."),
    ("none", "The kitchen was warm and bright."),
    ("none", "She closed the book and went to sleep."),
    ("none", "The weather was cold last winter."),
    ("none", "He played the piano for an hour."),
    ("none", "The light from the window was soft."),
    ("none", "We recorded the interview yesterday."),
    ("none", "The temperature dropped quickly at night."),
    ("none", "Tomorrow we will visit the museum."),
    ("none", "The bedroom has a large window."),
    ("none", "The next train leaves at noon."),
]


def phrases_in(text):
    """the list's phrases a text contains (longest first: ESP32-P4 is not also ESP32)"""
    found, rest = [], text.lower()
    for h in sorted(WORDS + SENTENCES, key=len, reverse=True):
        while h.lower() in rest:
            found.append(h)
            rest = rest.replace(h.lower(), " # ", 1)
    return found


def spoken(text):
    for w, s in SPOKEN:
        text = text.replace(w, s)
    return text


def main():
    voices = sys.argv[1]
    os.makedirs(os.path.join(HERE, "clips"), exist_ok=True)
    clips = []
    for vn in sorted(f[:-5] for f in os.listdir(voices) if f.endswith(".onnx")):
        voice = PiperVoice.load(os.path.join(voices, vn + ".onnx"))
        for i, (group, text) in enumerate(TEXTS):
            chunks = list(voice.synthesize(spoken(text)))
            a = np.concatenate([c.audio_float_array for c in chunks]).astype(np.float32)
            a = soxr.resample(a, chunks[0].sample_rate, 16000).astype(np.float32)
            a = np.concatenate([np.zeros(4800, np.float32), a, np.zeros(4800, np.float32)])   # 0.3 s each side
            pcm = np.clip(np.round(a * 32767.0), -32768, 32767).astype("<i2")
            cid = "%s_%02d" % (vn.replace("-", "_"), i)
            with wave.open(os.path.join(HERE, "clips", cid + ".wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(16000)
                w.writeframes(pcm.tobytes())
            clips.append({"id": cid, "voice": vn, "group": group, "text": text, "spoken": spoken(text),
                          "phrases": phrases_in(text)})
        print(vn, "done", flush=True)
    with open(os.path.join(HERE, "set.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"words": WORDS, "sentences": SENTENCES, "clips": clips}, f, indent=1)
    print(len(clips), "clips")


if __name__ == "__main__":
    main()
