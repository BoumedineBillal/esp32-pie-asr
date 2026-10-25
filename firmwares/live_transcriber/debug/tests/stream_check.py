# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""stream_check.py: the stream on the board, bit for bit with the engine on the PC.

The 8 clips of pieasr/tests/data fed through the live path (feed_begin, 1 s blocks as the microphone gives them,
feed_end), each against pieasr/tests/data/golden.json (the engine's host build): the encoder's CRC over every
chunk's output, and the beam search's text. Then the memory and the screen's cost meanwhile. Log: logs/stream_check.txt.

    python stream_check.py [--block 16000] [--screen live] [--repeat 1]
"""
import argparse
import json
import os
import re
import time

import numpy as np

import board

ap = argparse.ArgumentParser()
ap.add_argument("--block", type=int, default=16000, help="samples per feed (1 s; the board takes up to 16000)")
ap.add_argument("--screen", help="the board's screen during the clips (live, stats, history, phrases, settings, about)")
ap.add_argument("--repeat", type=int, default=1, help="the 8 clips this many times")
a = ap.parse_args()
log = board.logger("stream_check")
golden = json.load(open(os.path.join(board.DATA, "golden.json"), encoding="utf-8"))

b = board.ready(log)
log("ready:", b.cmd("info"))
if a.screen:
    log(b.cmd("screen " + a.screen))
b.cmd("uicheck reset")
t_ui = time.time()
same, ms = 0, []
for cid in board.CLIPS * a.repeat:
    r = board.feed(b, board.clip(cid), a.block)
    p = board.parse_feed(r)
    g = golden[cid]
    if not p:
        log("DIFF %s: %s" % (cid, r))
        continue
    chunks, crc, t, text, pieces = p
    ok = crc == g["crc"] and text == g["beam4"]
    same += ok
    ms.append(t)
    log("%s %s: %d chunks, %.1f ms per chunk (features %.1f, encoder %.1f, search %.1f), crc %s (PC %s): %s" % (
        "same" if ok else "DIFF", cid, chunks, *t, crc, g["crc"], text))
    if text != g["beam4"]:
        log("   the PC's beam 4:", g["beam4"])
        log("   the search's pieces:", pieces)
log("after the clips:", b.cmd("info"))
r = b.cmd("uicheck")
m = re.search(r"frames (\d+) mean_ms ([0-9.]+) max_ms ([0-9.]+)", r)
dt = time.time() - t_ui
log("the screen meanwhile: %s; %.1f frames/s, %.0f %% of core 1" % (
    r, int(m.group(1)) / dt, 100 * int(m.group(1)) * float(m.group(2)) / 1000 / dt))
log(b.cmd("stats"))
mean = np.mean(ms, 0) if ms else [0] * 4
n = len(board.CLIPS) * a.repeat
log("%d of %d clips the same; %.1f ms per chunk (features %.1f, encoder %.1f, search %.1f; means of the clips)" % (
    same, n, *mean))
log("PASS" if same == n else "FAIL")
b.close()
