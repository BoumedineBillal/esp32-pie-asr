# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""shots.py: every screen as a picture on the PC, for review and for compare_shots.py. The transcript is filled first
(the 8 clips joined with silences, through the live path, phrases off), then: each screen in the dark theme, the
phrases on (Phrases and Live), the Live screen scrolled back (a drag), the large text, and each screen in the light
theme; the settings are put back after. Then the board's text-fit counters. Pictures: shots/*.png.
"""
import os
import time

import board
from live_transcriber_pc import take_shot

OUT = os.path.join(board.HERE, "shots")
os.makedirs(OUT, exist_ok=True)
SCREENS = ("live", "stats", "history", "phrases", "settings", "about")


def shot(name):
    time.sleep(0.8)   # the screen's next frames
    print("saved", take_shot(b, os.path.join(OUT, name + ".png")), flush=True)


def tap(x, y):
    b.cmd("tap %d %d" % (x, y))
    time.sleep(0.4)


b = board.ready()
b.cmd("phrases off")
b.cmd("uicheck reset")
b.cmd("screen live")
x = board.joined()
b.cmd("feed_begin")   # board.feed, with a picture in the middle (listening: the pill, the grey word, the caret)
for k, i in enumerate(range(0, len(x), 16000)):
    blk = x[i:i + 16000]
    b.cmd("feed %d" % len(blk), payload=blk.tobytes(), timeout=60)
    if k == 33:
        shot("dark_live_listening")
print(b.cmd("feed_end", timeout=60)[:80], flush=True)
for s in SCREENS:
    b.cmd("screen " + s)
    shot("dark_" + s)
b.cmd("phrases on")
b.cmd("screen phrases")
shot("dark_phrases_on")
b.cmd("screen live")
shot("dark_live_phrases_on")   # the header's pill: "idle, phrases on"
b.cmd("phrases off")
b.cmd("screen live")
time.sleep(0.5)
b.cmd("drag 500 250 500 450")   # the text down: back in the transcript
time.sleep(0.8)
shot("dark_live_scrolled")
b.cmd("screen settings")
time.sleep(0.5)
tap(938, 76 + 3 * 65 + 30)         # Large
b.cmd("screen live")
shot("dark_live_large")
b.cmd("screen settings")
time.sleep(0.5)
tap(938 - 114, 76 + 3 * 65 + 30)   # Medium (the default)
tap(938, 76 + 4 * 65 + 30)         # Light
for s in SCREENS:
    b.cmd("screen " + s)
    shot("light_" + s)
b.cmd("screen settings")
time.sleep(0.5)
tap(938 - 114, 76 + 4 * 65 + 30)   # Dark again
b.cmd("screen live")
print(b.cmd("uicheck"))
b.close()
