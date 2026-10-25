# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""words_check.py: the board's words and lines against reference/word_rules.py on the same tokens.

Each stream goes through the board's live path (feed); its committed tokens with their frames (toks) then go through
word_rules.py on the PC; the words must agree in text, space before, line and end time. The streams: the 8 clips
alone, and the clips joined with silences under three sets of rules (time-out / pause: 0.5 / 1.5 s, the defaults;
0.2 / 0.8; 1.0 / 2.5). The phrases are switched off meanwhile (their rewrites are phrases_check's). Log:
logs/words_check.txt.
"""
import os
import sys

import board

sys.path.insert(0, os.path.join(board.HERE, "reference"))
from word_rules import words_of  # noqa: E402

log = board.logger("words_check")
b = board.ready(log)
was_on = b.cmd("phrases").startswith("ok phrases on")
b.cmd("phrases off")
pieces = board.vocab(b)
log("vocabulary: %d pieces" % len(pieces))
x = board.joined()
cases = [("%s alone, 0.5 / 1.5" % cid, board.clip(cid), 0.5, 1.5) for cid in board.CLIPS]
cases += [("joined (%.0f s), %.1f / %.1f" % (len(x) / 16000, t, p), x, t, p) for t, p in ((0.5, 1.5), (0.2, 0.8),
                                                                                          (1.0, 2.5))]
same = 0
for name, audio, timeout_s, pause_s in cases:
    b.cmd("rules %g %g" % (timeout_s, pause_s))
    board.feed(b, audio)
    frames, tk = board.toks(b)
    got = [(text, sp, line, e) for _, line, e, _, sp, text in board.words(b)]
    want, lines = words_of(pieces, frames, tk, timeout_s, pause_s)
    ok = got == want
    same += ok
    log("%s %s: %d frames, %d tokens, %d words, %d lines (the rules: %d words, %d lines)" % (
        "same" if ok else "DIFF", name, frames, len(tk), len(got), 1 + max([w[2] for w in got] or [0]), len(want),
        len(lines)))
    if not ok:
        for i in range(max(len(got), len(want))):
            g = got[i] if i < len(got) else None
            w = want[i] if i < len(want) else None
            if g != w:
                log("   word %d: board %s, the rules %s" % (i, g, w))
                break
b.cmd("rules 0.5 1.5")
if was_on:
    b.cmd("phrases on")
log("%d of %d streams the same" % (same, len(cases)))
log("PASS" if same == len(cases) else "FAIL")
b.close()
