# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""compare_shots.py: the screens against the reference pictures (reference/shots, reviewed; taken by shots.py from its
fixed state). Only the regions that do not depend on time are compared (the transcript, the header, the buttons,
History's list, Settings, Phrases, About's fixed paragraphs); the tiles, the level meter and the numbers are not.
Prints the differing pixels of each region.

    python compare_shots.py [shots folder] [reference folder]      (defaults: shots, reference/shots)
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = ("header", (0, 0, 1024, 68))
BUTTONS = ("Start / Stop / Clear", (0, 68, 425, 64))
TEXT = ("the transcript", (34, 150, 948, 304))
REGIONS = {
    "live": [HEADER, BUTTONS, TEXT],
    "live_scrolled": [HEADER, BUTTONS, TEXT],
    "live_large": [HEADER, BUTTONS, TEXT],
    "stats": [HEADER],
    "history": [HEADER, ("the summary", (0, 68, 800, 56)), ("the list", (16, 124, 992, 468))],
    "settings": [("the whole screen", (0, 0, 1024, 600))],
    "phrases": [("the whole screen", (0, 0, 1024, 600))],
    "phrases_on": [("the whole screen", (0, 0, 1024, 600))],
    "live_phrases_on": [HEADER, BUTTONS, TEXT],
    "about": [HEADER, ("the fixed paragraphs", (16, 76, 992, 300))],
}


def load(path):
    try:
        import cv2
        return cv2.imread(path)[:, :, ::-1]
    except ImportError:
        from PIL import Image
        return np.asarray(Image.open(path).convert("RGB"))


def main():
    shots = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "shots")
    ref = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "reference", "shots")
    total, compared = 0, 0
    for theme in ("dark", "light"):
        for screen, regions in REGIONS.items():
            name = "%s_%s.png" % (theme, screen)
            a, b = os.path.join(shots, name), os.path.join(ref, name)
            if not (os.path.exists(a) and os.path.exists(b)):
                continue
            x, y = load(a), load(b)
            for label, (rx, ry, rw, rh) in regions:
                d = int(np.count_nonzero(np.any(x[ry:ry + rh, rx:rx + rw] != y[ry:ry + rh, rx:rx + rw], axis=2)))
                total += d
                compared += 1
                print("%-28s %-22s %7d pixels differ of %d" % (name, label, d, rw * rh))
    print("%d regions compared, %d pixels differ" % (compared, total))
    print("PASS" if compared and total == 0 else "FAIL")


if __name__ == "__main__":
    main()
