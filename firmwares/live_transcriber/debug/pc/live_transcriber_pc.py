# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""live_transcriber_pc.py: the PC side of a debug build (sdkconfig.debug). Keeps the board company over USB.

    python live_transcriber_pc.py                  send the model if the board has none on its SD card, then show the
                                                   board's log and append every final word to
                                                   transcripts/session_<time>.txt; Ctrl+C stops
    python live_transcriber_pc.py --seconds 30     the same for 30 s
    python live_transcriber_pc.py --shot out.png   a screenshot of the board's screen, then exit

While this runs it pings the board every second, so the board may send its log and words.
Needs numpy (and OpenCV or Pillow for --shot).
"""
import argparse
import datetime
import os
import struct
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "components", "debug", "pclink", "python"))
from pclink import Link  # noqa: E402

TRANSCRIPTS = os.path.join(HERE, "transcripts")


def screenshot(p):
    """the screen as an RGB picture (the board sends its panel's pixels, which its rotation turns)"""
    w, h, rot, _ = struct.unpack_from("<4H", p, 0)
    px = np.frombuffer(p, "<u2", w * h, 8).reshape(h, w)
    rgb = np.stack([(px >> 11 & 31) * 255 // 31, (px >> 5 & 63) * 255 // 63, (px & 31) * 255 // 31], -1)
    rgb = rgb.astype(np.uint8)
    return np.rot90(rgb, k=-(rot // 90)) if rot else rgb


def save_png(img, path):
    try:
        import cv2
        cv2.imwrite(path, img[:, :, ::-1])
    except ImportError:
        from PIL import Image
        Image.fromarray(img).save(path)


def take_shot(link, path, timeout=10):
    link.send("shot")
    end = time.time() + timeout
    while time.time() < end:
        magic, p = link.message(1)
        if magic == b"SHT1":
            save_png(screenshot(p), path)
            return path
    raise TimeoutError("no screenshot")


def lines_text(words):
    """{word number: (line, space before, text)} as text, a line per board line"""
    out, seg = [], None
    for i in sorted(words):
        s, sp, text = words[i]
        out.append(("\n" if seg is not None else "") if s != seg else (" " if sp else ""))
        out.append(text)
        seg = s
    return "".join(out)


def stamp():
    return datetime.datetime.now().strftime("%Y%m%d_%H%M%S")


class Session:
    """the board's messages: the log printed, each final word appended to session_<time>.txt (a line per board
    line, with the PC's clock)"""

    def __init__(self, folder=TRANSCRIPTS, log=print):
        os.makedirs(folder, exist_ok=True)
        self.log = log
        self.path = os.path.join(folder, "session_%s.txt" % stamp())
        self.f = open(self.path, "w", encoding="utf-8")
        self.words = {}            # the board's session: word number -> (line, space, text)
        self.last_i = self.last_seg = -1

    def handle(self, magic, p):
        if magic in (b"LOG0", b"TXT0"):
            self.log("board:", p.decode(errors="replace"))
        elif magic == b"WRD0":
            i, seg, end_ms, final_ms, space, text = p.decode(errors="replace").split(" ", 5)
            i, seg = int(i), int(seg)
            if i <= self.last_i:   # the numbers start again: the board cleared its transcript
                self.words = {}
                self.f.write("\n")
                self.last_seg = -1
            self.words[i] = (seg, int(space), text)
            if seg != self.last_seg:
                self.f.write(("\n" if self.last_i >= 0 else "") + "[%s] " % time.strftime("%H:%M:%S"))
            elif int(space):
                self.f.write(" ")
            self.f.write(text)
            self.f.flush()
            self.last_i, self.last_seg = i, seg

    def text(self):
        return lines_text(self.words)

    def close(self):
        self.f.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", help="save a screenshot here and exit")
    ap.add_argument("--seconds", type=float, help="stop after this long")
    a = ap.parse_args()

    def log(*x):
        print(*x, flush=True)

    log("looking for the board on USB...")
    link = Link(wait_s=60)
    link.start_pings()
    s = None
    try:
        log("board:", link.ensure_model(log=log))
        if a.shot:
            log("saved", take_shot(link, a.shot))
            return
        s = Session(log=log)
        log("listening to the board; final words go to", s.path, "(Ctrl+C stops)")
        end = time.time() + a.seconds if a.seconds else None
        while end is None or time.time() < end:
            magic, p = link.message(1)
            if magic:
                s.handle(magic, p)
    except KeyboardInterrupt:
        pass
    finally:
        if s:
            s.close()
        link.close()


if __name__ == "__main__":
    main()
