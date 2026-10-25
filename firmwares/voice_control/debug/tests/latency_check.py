# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""latency_check.py [demo]: the time from the end of an order to its action, on a demo's page (default the vacuum).

Command clips of demos/<demo>/ (5 per voice) fed at the speed of real time (0.1 s blocks on the PC's clock), each followed
by 3 s of silence as a speaker would leave, so the stream runs as with the microphone. The PC asks the demo's
utterance count after every block; the delay is the PC's time when the action is there minus the time the last word
ended (the transcript's end_ms of it, on the same clock). Target: every delay under 2.5 s. Log:
logs/latency_check.txt; the last line: PASS or FAIL.
"""
import json
import os
import re
import statistics
import sys
import time

import numpy as np

import board

DEMO = sys.argv[1] if len(sys.argv) > 1 else "vacuum"
V = os.path.join(board.HERE, "demos", DEMO)
BLOCK = 1600
TARGET_S = 2.5
log = board.logger("latency_check" if DEMO == "vacuum" else "latency_check_" + DEMO)


def last_word_end(b):
    """the end of the last word with letters (a "." after it can come out later than the speech's end)"""
    b.send("words")
    end, last = time.time() + 5, None
    while time.time() < end:
        magic, p = b.message(1)
        if magic == b"WRD0":
            f = p.decode("utf-8").split(" ", 5)
            if re.search(r"[A-Za-z0-9]", f[5]):
                last = (int(f[2]), int(f[3]))   # end_ms, final_ms
        elif magic == b"TXT0" and p.startswith(b"ok words"):
            return last
    raise TimeoutError("no words")


clips = [c for c in json.load(open(os.path.join(V, "set.json"), encoding="utf-8"))["clips"] if c["group"] == "command"]
per_voice = {}
for c in clips:
    per_voice.setdefault(c["voice"], [])
    if len(per_voice[c["voice"]]) < 5:
        per_voice[c["voice"]].append(c)
chosen = [c for v in per_voice.values() for c in v]

b = board.ready(log)
log(b.cmd("screen " + DEMO))
time.sleep(0.5)
delays, rows = [], []
for c in chosen:
    x = np.concatenate([board.wav(os.path.join(V, "clips", c["id"] + ".wav")), np.zeros(3 * 16000, "<i2")])
    before = board.demo(b)["count"]
    b.cmd("feed_begin")
    t0, seen, v = time.time(), None, None
    for i in range(0, len(x), BLOCK):
        wait = t0 + i / 16000.0 - time.time()
        if wait > 0:
            time.sleep(wait)
        b.cmd("feed %d" % len(x[i:i + BLOCK]), payload=x[i:i + BLOCK].tobytes(), timeout=60)
        if seen is None:
            v = board.demo(b)
            if v["count"] > before:
                seen = time.time()
    b.cmd("feed_end", timeout=60)
    ends = last_word_end(b)
    if seen is None or ends is None:
        log("  %-24s no action within the clip and 3 s of silence (last word end, final: %s)" % (c["id"], ends))
        rows.append(None)
        continue
    end_ms, final_ms = ends
    d = seen - (t0 + end_ms / 1000.0)
    delays.append(d)
    log("  %-24s %.2f s  (board's own measure %d ms, the word final %d ms after its end)  %s | %s" % (
        c["id"], d, v["delay_ms"], final_ms - end_ms, v["actions"], v["words"]))
b.close()
if delays:
    log("%d orders: delay median %.2f s, max %.2f s (target: under %.1f s)" % (
        len(delays), statistics.median(delays), max(delays), TARGET_S))
ok = len(delays) == len(chosen) and max(delays) < TARGET_S
log("PASS" if ok else "FAIL")
