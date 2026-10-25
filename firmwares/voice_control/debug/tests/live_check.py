# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""live_check.py: the board's microphone live for some minutes on a demo's page, nothing played, its world kept busy
(an order again every 40 s: the factory's line running, its belt drawn every frame, by default; the vacuum cleaning
everything). Checks: compute under 0.85x real time, the stream keeps up (behind bounded), no frames lost, the memory
constant, internal RAM above 40 KB, the screen's frames. Polls the board's stats every 10 s. Log: logs/live_check.txt;
the last line: PASS or FAIL.

    python live_check.py [--minutes 3] [--demo factory]
"""
import argparse
import re
import time

import board

ap = argparse.ArgumentParser()
ap.add_argument("--minutes", type=float, default=3)
ap.add_argument("--demo", default="factory")
a = ap.parse_args()
BUSY = {"factory": "reset the alarm and start the line", "vacuum": "clean everything",
        "presenter": "next slide", "smarthome": "open the blinds", "car": "navigate home",
        "kitchen": "preheat the oven"}
log = board.logger("live_check")


def stats():
    r = b.cmd("stats")
    return {k: float(v) for k, v in re.findall(r"(\w+) (-?[0-9.]+)", r[len("ok stats"):])}


b = board.ready(log)
log("before:", b.cmd("info"))
b.cmd("screen " + a.demo)
b.cmd("tap %d 34" % (1024 - 16 - 150 - 10 - 55))   # Reset
time.sleep(0.5)
b.cmd("uicheck reset")
log(b.cmd("start"))
t0 = time.time()
rows, said = [], 0
while time.time() - t0 < a.minutes * 60:
    if time.time() - t0 >= said * 40:
        board.say(b, BUSY[a.demo])
        said += 1
    time.sleep(10)
    s = stats()
    rows.append(s)
    v = board.demo(b)
    log("%5.0f s: chunks %d, audio %.1f s, behind %d ms, x_rt %.3f, chunk mean %d max %d ms, lost %d, words %d, "
        "PSRAM free %d (lowest %d), internal %d | utterances %d, world: %s" % (
            time.time() - t0, s["chunks"], s["listening"] / 1000, s["behind"], s["x_rt"], s["mean"], s["max"],
            s["lost"], s["words"], s["psram"], s["psram_min"], s["internal"], v["count"], v["status"]))
log(b.cmd("stop"))
time.sleep(3)
log("after:", b.cmd("info"))
ui = b.cmd("uicheck")
log(ui)
first, last = rows[0], rows[-1]
behind_max = max(r["behind"] for r in rows)
x_rt_max = max(r["x_rt"] for r in rows)
ok_rt = x_rt_max <= 0.85 and behind_max < 2 * 1280
ok_lost = last["lost"] == 0
ok_mem = abs(last["psram"] - first["psram"]) < 64 * 1024 and last["internal"] >= first["internal"] - 8 * 1024
ok_internal = min(r["internal"] for r in rows) >= 40 * 1024
log("%.1f min: %d chunks for %.1f s of audio, x_rt at most %.3f, behind at most %d ms, %d frames lost; PSRAM free "
    "%d -> %d (lowest %d), internal %d -> %d" % (
        (time.time() - t0) / 60, last["chunks"], last["listening"] / 1000, x_rt_max, behind_max, last["lost"],
        first["psram"], last["psram"], last["psram_min"], first["internal"], last["internal"]))
log("real time (<= 0.85x): %s, nothing lost: %s, memory constant: %s, internal RAM above 40 KB: %s" % (
    ok_rt, ok_lost, ok_mem, ok_internal))
log("PASS" if ok_rt and ok_lost and ok_mem and ok_internal and "overflows 0 " in ui else "FAIL")
b.close()
