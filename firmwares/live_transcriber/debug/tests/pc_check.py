# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""pc_check.py: the PC tool's transcript file, History's "Save to SD card", and the settings across a reboot; then
the PC tool itself from the board's boot. Log: logs/pc_check.txt.

1. Two clips 2 s apart through the live path while live_transcriber_pc's Session takes the board's messages: its
   session file has the board's words, line by line.
2. History's "Save to SD card" tapped: a new transcript_NNN.txt on the card, read back over the link, has the board's
   lines, each after its start time ("[m:ss] "); the file is removed after.
3. Settings: Large tapped, the board rebooted; after the boot the size is still Large; Medium tapped back.
4. live_transcriber_pc.py --seconds 25 from the board's boot: the board idle, its model from the SD card (or sent by
   the tool when the card has none).
"""
import os
import re
import subprocess
import sys
import time

import numpy as np

import board
from live_transcriber_pc import Session, lines_text
from pclink import Link

log = board.logger("pc_check")
results = []


def check(name, ok, detail=""):
    results.append(ok)
    log("%s %s%s" % ("PASS" if ok else "FAIL", name, (": " + detail) if detail else ""))


def pump(b, secs):
    end = time.time() + secs
    while time.time() < end:
        b.message(0.3)


def tap(b, x, y):
    b.cmd("tap %d %d" % (x, y))
    pump(b, 0.6)


def transcripts(b):
    return set(re.findall(r" (transcript_\d+\.txt) ", b.cmd("sd_list")))


b = board.ready(log)
os.makedirs(os.path.join(board.LOGS, "pc_check"), exist_ok=True)
s = Session(folder=os.path.join(board.LOGS, "pc_check"), log=log)
b.on_message = s.handle
x = np.concatenate([board.clip(board.CLIPS[0]), np.zeros(32000, "<i2"), board.clip(board.CLIPS[1])])
log(board.feed(b, x)[:60])
pump(b, 1.0)
s.f.flush()
lines = [l.split("] ", 1)[1] for l in open(s.path, encoding="utf-8").read().strip().split("\n") if "] " in l]
b.on_message = None   # (the words asked again would look to the Session like a new transcript)
ws = board.words(b)
want = lines_text({i: (line, sp, t) for i, line, _, _, sp, t in ws})
check("1. the session file has the board's words", "\n".join(lines) == want,
      "%d lines, %d words" % (len(lines), len(ws)))
s.close()

before = transcripts(b)
b.cmd("screen history")
pump(b, 0.8)
tap(b, 908, 96)   # Save to SD card
pump(b, 2.0)
new = sorted(transcripts(b) - before)
saved = ""
if new:
    r = b.cmd("sd_get " + new[-1])
    saved = r.split(" |", 1)[1] if " |" in r else ""
    log(b.cmd("sd_rm " + new[-1]))
rows = saved.strip().split("\n") if saved.strip() else []
stamped = all(re.match(r"\[\d+:\d\d\] ", l) for l in rows)
texts = "\n".join(l.split("] ", 1)[1] for l in rows if "] " in l)
check("2. Save to SD card: the file has the board's lines, each after its time", bool(new) and stamped and
      texts == want, new[-1] if new else "no new file")

b.cmd("screen settings")
pump(b, 0.8)
tap(b, 938, 301)   # Large
before = b.cmd("settings")
log(before)
log(b.cmd("reboot"))
b.close()
time.sleep(4)
b = Link(wait_s=40)
b.start_pings()
log(b.cmd("hello"))
after = b.cmd("settings")
log(after)
check("3. the settings across a reboot", "size 2" in before and after == before)
b.cmd("screen settings")
pump(b, 0.8)
tap(b, 824, 301)   # Medium
check("3b. Medium again", "size 1" in b.cmd("settings"))
log(b.cmd("reboot"))
b.close()
time.sleep(4)

tool = os.path.join(board.HERE, "..", "pc", "live_transcriber_pc.py")
r = subprocess.run([sys.executable, tool, "--seconds", "25"], capture_output=True, text=True, timeout=120)
out = r.stdout
log(out.strip())
b = Link(wait_s=30)
b.start_pings()
hello = b.cmd("hello")
from_card = "the model from the microSD card" in hello
check("4. the model at the board's boot: from the SD card, or sent by live_transcriber_pc.py",
      "idle" in hello and (from_card or ("sending the model" in out and "ok load" in out)), hello)
b.close()
log("%d of %d PASS" % (sum(results), len(results)))
log("PASS" if all(results) else "FAIL")
