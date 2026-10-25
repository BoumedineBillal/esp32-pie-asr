# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""live_check.py: the board's microphone live for some minutes, nothing played: the stream keeps up (behind
bounded), no frames lost, the memory constant, the words final in time. Polls the board's stats every 10 s. Log:
logs/live_check.txt.

    python live_check.py [--minutes 3]
"""
import argparse
import re
import time

import board

ap = argparse.ArgumentParser()
ap.add_argument("--minutes", type=float, default=3)
a = ap.parse_args()
log = board.logger("live_check")


def stats():
    r = b.cmd("stats")
    return {k: float(v) for k, v in re.findall(r"(\w+) (-?[0-9.]+)", r[len("ok stats"):])}


b = board.ready(log)
log("before:", b.cmd("info"))
b.cmd("screen live")
b.cmd("uicheck reset")
log(b.cmd("start"))
t0 = time.time()
rows = []
while time.time() - t0 < a.minutes * 60:
    time.sleep(10)
    s = stats()
    rows.append(s)
    log("%5.0f s: chunks %d, audio %.1f s, behind %d ms, x_rt %.3f, chunk mean %d max %d ms, lost %d, clipped %d, "
        "words %d (delay median %d, p95 %d ms), PSRAM free %d (lowest %d), internal %d" % (
            time.time() - t0, s["chunks"], s["listening"] / 1000, s["behind"], s["x_rt"], s["mean"], s["max"],
            s["lost"], s["clipped"], s["words"], s["delay_median"], s["delay_p95"], s["psram"], s["psram_min"],
            s["internal"]))
log(b.cmd("stop"))
time.sleep(3)
log("after:", b.cmd("info"))
log(b.cmd("uicheck"))
first, last = rows[0], rows[-1]
behind_max = max(r["behind"] for r in rows)
ok_rt = behind_max < 2 * 1280
ok_lost = last["lost"] == 0
ok_mem = abs(last["psram"] - first["psram"]) < 64 * 1024 and last["internal"] >= first["internal"] - 8 * 1024
log("%.1f min: %d chunks for %.1f s of audio (%.1f expected), behind at most %d ms, %d frames lost, "
    "chunk compute mean %d ms, max %d ms; PSRAM free %d -> %d (lowest %d), internal %d -> %d" % (
        (time.time() - t0) / 60, last["chunks"], last["listening"] / 1000, last["listening"] / 1280.0, behind_max,
        last["lost"], last["mean"], last["max"], first["psram"], last["psram"], last["psram_min"], first["internal"],
        last["internal"]))
# a word is final within the beam search's commit lag (3 s), the chunk's 1.28 s and the word time-out
ok_delay = last["words"] == 0 or last["delay_p95"] < 6000
log("real time: %s, nothing lost: %s, memory constant: %s, words final in time: %s (%d words, delay p95 %d ms)" % (
    ok_rt, ok_lost, ok_mem, ok_delay, last["words"], last["delay_p95"]))
log("PASS" if ok_rt and ok_lost and ok_mem and ok_delay else "FAIL")
b.close()
