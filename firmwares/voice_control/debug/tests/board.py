# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""board.py: what voice_control's tests share: the link with the model loaded and the board idle, a stream fed
through the live path, the demo's last utterance, screenshots. The tests run on Windows (WinUSB) with a debug build
on the board (sdkconfig.debug).

    b = board.ready(log)            # Link; sends the model if the board has none on its SD card
    r = board.feed(b, pcm16)        # feed_begin, 1 s blocks, feed_end: the reply line
    v = board.demo(b)               # the selected demo's last utterance: {id, count, delay_ms, actions, steps,
                                    # status, words}
    v = board.say(b, text)          # an order as if heard, and the utterance it made
    board.shot(b, path)             # the screen as a PNG
"""
import os
import struct
import sys
import time
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
LOGS = os.path.join(HERE, "logs")
sys.path.insert(0, os.path.join(REPO, "components", "debug", "pclink", "python"))
from pclink import Link  # noqa: E402


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


def wav(path):
    with wave.open(path) as w:
        assert w.getframerate() == 16000 and w.getnchannels() == 1 and w.getsampwidth() == 2
        return np.frombuffer(w.readframes(w.getnframes()), "<i2")


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


def demo(b):
    """'ok demo <id> <utterances> <delay ms> | <actions> | <steps> | <status> | <words>' as a dict"""
    r = b.cmd("demo")
    head, actions, steps, status, words = r.split(" | ", 4)
    _, _, did, count, delay = head.split()
    return {"id": did, "count": int(count), "delay_ms": int(delay), "actions": actions,
            "steps": steps.split(",") if steps else [], "status": status, "words": words}


def wait_utterance(b, before, timeout=5.0):
    """the demo's state once it has more than `before` utterances (None after timeout)"""
    end = time.time() + timeout
    while time.time() < end:
        v = demo(b)
        if v["count"] > before:
            return v
        time.sleep(0.05)
    return None


def settled(b, before, timeout=4.0, quiet=0.6):
    """the demo's state once it has more than `before` utterances and no new one for `quiet` s (after timeout: as
    it is)"""
    end = time.time() + timeout
    v = demo(b)
    count, changed = v["count"], time.time()
    while time.time() < end:
        time.sleep(0.05)
        v = demo(b)
        if v["count"] != count:
            count, changed = v["count"], time.time()
        elif count > before and time.time() - changed >= quiet:
            break
    return v


def say(b, text, timeout=5.0):
    before = demo(b)["count"]
    b.cmd("say " + "_".join(text.split()))
    return wait_utterance(b, before, timeout)


def screenshot(p):
    """the screen as an RGB picture (the board sends its panel's pixels, which its rotation turns)"""
    w, h, rot, _ = struct.unpack_from("<4H", p, 0)
    px = np.frombuffer(p, "<u2", w * h, 8).reshape(h, w)
    rgb = np.stack([(px >> 11 & 31) * 255 // 31, (px >> 5 & 63) * 255 // 63, (px & 31) * 255 // 31], -1)
    rgb = rgb.astype(np.uint8)
    return np.rot90(rgb, k=-(rot // 90)) if rot else rgb


def shot(b, path, timeout=10):
    b.send("shot")
    end = time.time() + timeout
    while time.time() < end:
        magic, p = b.message(1)
        if magic == b"SHT1":
            from PIL import Image
            Image.fromarray(np.ascontiguousarray(screenshot(p))).save(path)
            return path
    raise TimeoutError("no screenshot")
