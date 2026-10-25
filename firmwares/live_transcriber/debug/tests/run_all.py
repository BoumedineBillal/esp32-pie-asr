# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""run_all.py: every board check without sound, one after the other, each in its own process (each sends the model
if the board has none on its SD card). Windows Python; the board's USB High-Speed port; a debug build on the board
(sdkconfig.debug). Log: logs/run_all.txt.

    python run_all.py [--minutes 3]      live_check's length

stream_check (the 8 clips: the encoder and the texts bit for bit with the engine on the PC), words_check (the word
rules), phrases_check (the phrases: 140 text-to-speech clips, the board = the PC), pc_check (the PC tool, Save to SD
card, the settings across a reboot, the boot), shots + compare_shots (every screen, no text cut, the same pixels as
the reference), live_check (the microphone live).
"""
import argparse
import os
import subprocess
import sys
import time

import board

ap = argparse.ArgumentParser()
ap.add_argument("--minutes", type=float, default=3)
a = ap.parse_args()
CHECKS = [("stream_check.py", []), ("words_check.py", []), ("phrases_check.py", []), ("pc_check.py", []),
          ("shots.py", []), ("compare_shots.py", []), ("live_check.py", ["--minutes", str(a.minutes)])]
log = board.logger("run_all")
rows = []
for name, args in CHECKS:
    t0 = time.time()
    r = subprocess.run([sys.executable, os.path.join(board.HERE, name)] + args, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    lines = [l for l in r.stdout.strip().split("\n") if l]
    last = lines[-1] if lines else ""
    if name == "shots.py":   # no verdict of its own: the board's text-fit count after all the pictures
        ok = r.returncode == 0 and "overflows 0 " in last
    else:
        ok = r.returncode == 0 and last == "PASS"
    rows.append(ok)
    log("%s %-16s %5.0f s  %s" % ("PASS" if ok else "FAIL", name, time.time() - t0,
                                   lines[-2] if len(lines) > 1 and name != "shots.py" else last))
    if not ok:
        log(r.stdout[-1500:] + r.stderr[-1500:])
log("%d of %d PASS" % (sum(rows), len(rows)))
log("PASS" if all(rows) else "FAIL")
