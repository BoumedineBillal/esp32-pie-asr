# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""board.py: what the tests share: the paths, the link with the model loaded and the board idle, a stream fed through
the live path, the transcript's words. The tests run on Windows (WinUSB) with a debug build on the board.

    b = board.ready(log)            # Link; sends the model if the board has none on its SD card
    r = board.feed(b, pcm16)        # feed_begin, 1 s blocks, feed_end: the reply line
    words = board.words(b)          # [(i, line, end_ms, final_ms, space, text)]
"""
import os
import re
import sys
import time
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
DATA = os.path.join(REPO, "pieasr", "tests", "data")   # the 8 clips, refs.json, golden.json
LOGS = os.path.join(HERE, "logs")
sys.path.insert(0, os.path.join(REPO, "components", "debug", "pclink", "python"))
sys.path.insert(0, os.path.join(HERE, "..", "pc"))
from pclink import Link  # noqa: E402

CLIPS = ["1272-128104-%s" % s for s in ("0000", "0001", "0003", "0005", "0006", "0007", "0008", "0010")]
GAPS = [2.0, 0.6, 3.0, 1.2, 2.0, 0.6, 3.0]   # the silences between the clips joined


def logger(name):
    """log(*x): printed and written to logs/<name>.txt"""
    os.makedirs(LOGS, exist_ok=True)
    f = open(os.path.join(LOGS, name + ".txt"), "w", encoding="utf-8", newline="\n")

    def log(*x):
        s = " ".join(str(v) for v in x)
        print(s, flush=True)
        f.write(s + "\n")
        f.flush()
    return log


def clip(cid, folder=DATA):
    with wave.open(os.path.join(folder, cid + ".wav")) as w:
        assert w.getframerate() == 16000 and w.getnchannels() == 1 and w.getsampwidth() == 2
        return np.frombuffer(w.readframes(w.getnframes()), "<i2")


def joined():
    """the 8 clips with the silences between them"""
    x = [clip(CLIPS[0])]
    for g, cid in zip(GAPS, CLIPS[1:]):
        x += [np.zeros(int(g * 16000), "<i2"), clip(cid)]
    return np.concatenate(x)


def ready(log=print):
    b = Link(wait_s=30)
    b.start_pings()
    log(b.ensure_model(log=log))
    hello, t0 = b.cmd("hello"), time.time()
    while "idle" not in hello:
        if time.time() - t0 > 120 or "failed" in hello:
            sys.exit("the board is not ready: " + hello)
        time.sleep(0.5)
        hello = b.cmd("hello")
    return b


def feed(b, x, block=16000):
    b.cmd("feed_begin")
    for i in range(0, len(x), block):
        blk = np.asarray(x[i:i + block], "<i2")
        b.cmd("feed %d" % len(blk), payload=blk.tobytes(), timeout=60)
    return b.cmd("feed_end", timeout=60)


FEED = re.compile(r"ok feed (\d+) ([0-9a-f]{8}) ([0-9.]+) ([0-9.]+) ([0-9.]+) ([0-9.]+) \| (.*)$", re.S)


def parse_feed(r):
    """feed_end's reply: (chunks, crc, [ms per chunk, features, encoder, search], text, pieces) or None"""
    pieces = ""
    if " || " in r:
        r, pieces = r[:r.rindex(" || ")], r[r.rindex(" || ") + 4:]
    m = FEED.match(r)
    if not m:
        return None
    return int(m.group(1)), m.group(2), [float(m.group(i)) for i in (3, 4, 5, 6)], m.group(7).strip(), pieces


def words(b, timeout=20):
    b.send("words")
    out, end = [], time.time() + timeout
    while time.time() < end:
        magic, p = b.message(1)
        if magic == b"WRD0":
            i, line, e, f, sp, text = p.decode("utf-8").split(" ", 5)
            out.append((int(i), int(line), int(e), int(f), int(sp), text))
        elif magic == b"TXT0" and p.startswith(b"ok words"):
            n = int(p.split()[2])
            if n != len(out):
                raise RuntimeError("words: %d sent of %d" % (len(out), n))
            return out
    raise TimeoutError("no words")


def vocab(b):
    return b.cmd("vocab").split("\n")[1:]


def toks(b):
    m = re.match(r"ok toks (\d+) \|(.*)$", b.cmd("toks"), re.S)
    return int(m.group(1)), [(int(f), int(k)) for f, k in (t.split(":") for t in m.group(2).split())]
