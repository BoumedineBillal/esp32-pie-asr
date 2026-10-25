# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""demo_check.py [demo...]: each demo on the board = on the PC (all six by default).

For each demo:
  1. its page open: its phrase list in use (list 1 + the demo's index, on);
  2. each sentence of its rules' host test (components/voice_rules/host/test_vacuum_rules.cpp for the vacuum,
     test_demo_rules.cpp for the others) given to the board's demo as if heard ("say"): the board's actions = the
     test's;
  3. the 188 clips of demos/<demo>/ (make_clips.py: 100 commands, 88 near misses) fed through the live path, each its
     own stream: one utterance per clip, its actions = the PC's (demos/<demo>/pc_phrases_actions.tsv: the engine's
     host build with the same list, then the rules), and the score against set.json (exact commands, false actions).

Log: logs/demo_check.txt. The last line: PASS or FAIL.
"""
import json
import os
import re
import sys
import time

import board

D = os.path.join(board.HERE, "demos")
sys.path.insert(0, D)
from texts import same  # noqa: E402

ORDER = ["vacuum", "smarthome", "car", "factory", "kitchen", "presenter"]
HOST = os.path.join(board.REPO, "components", "voice_rules", "host")
log = board.logger("demo_check")


def load_tsv(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        out[os.path.basename(f[0]).rsplit(".", 1)[0]] = f[-1]
    return out


def done(actions):
    """the actions to do (not skipped) in a describe() string"""
    return [a for a in actions.split(" (")[0].split("; ") if a and not a.startswith("-") and not a.startswith("(")]


def norm(text):
    return " ".join(re.sub(r"[^a-z0-9' ]", " ", text.lower()).split())


def say_cases(demo):
    """the host test's (text, expected actions) of a demo"""
    if demo == "vacuum":
        src = open(os.path.join(HOST, "test_vacuum_rules.cpp"), encoding="utf-8").read()
        return [(t, e) for t, e in re.findall(r'\{"([^"]*)",\s*"([^"]*)"\}', src) if t.strip()]
    src = open(os.path.join(HOST, "test_demo_rules.cpp"), encoding="utf-8").read()
    return [(t, e) for d, t, e in re.findall(r'\{"(\w+)",\s*"([^"]*)",\s*"([^"]*)"\}', src) if d == demo]


def check(b, demo):
    fails = []
    log("== %s" % demo)
    log(b.cmd("screen " + demo))
    time.sleep(0.6)
    pl = b.cmd("phrases_list")
    log(pl)
    if not pl.startswith("ok phrases_list %d of 7," % (1 + ORDER.index(demo))) or not pl.endswith(", on"):
        fails.append("phrase list")
    if board.demo(b)["id"] != demo:
        fails.append("not selected")
    # the host test, said
    cases = say_cases(demo)
    bad = 0
    for text, expect in cases:
        v = board.say(b, text)
        got = v["actions"] if v else "(no utterance)"
        if got != expect:
            bad += 1
            log("  say %-60s got %s, expected %s" % (text, got, expect))
    log("say: %d of %d as the host test" % (len(cases) - bad, len(cases)))
    if bad:
        fails.append("say")
    # the clips
    clips = json.load(open(os.path.join(D, demo, "set.json"), encoding="utf-8"))["clips"]
    pc = load_tsv(os.path.join(D, demo, "pc_phrases_actions.tsv"))
    pc_text = load_tsv(os.path.join(D, demo, "pc_phrases.tsv"))
    b.cmd("tap %d 34" % (1024 - 16 - 150 - 10 - 55))   # Reset: the world as at the start
    time.sleep(0.5)
    same_n = exact = n_cmd = fa = n_nm = split = text_diff = 0
    t0 = time.time()
    for c in clips:
        x = board.wav(os.path.join(D, demo, "clips", c["id"] + ".wav"))
        before = board.demo(b)["count"]
        board.feed(b, x)
        v = board.settled(b, before)
        if v["count"] == before:   # no words at all: no utterance (the PC's rules give "(no_words)" for an empty text)
            v = {"count": before, "actions": "(no_words)", "words": ""}
        if v["count"] > before + 1:
            split += 1
            log("  %-24s %d utterances (a fragment, then with the words after it): the last one is compared" % (
                c["id"], v["count"] - before))
        if norm(v["words"]) != norm(pc_text.get(c["id"], "")):
            text_diff += 1
            log("  %-24s words  board %r, PC %r" % (c["id"], v["words"], pc_text.get(c["id"], "")))
        got, want = v["actions"], pc.get(c["id"], "(missing)")
        if got == want:
            same_n += 1
        else:
            log("  %-24s actions board %s, PC %s" % (c["id"], got, want))
        if c["group"] == "command":
            n_cmd += 1
            exact += same(demo, got) == same(demo, c["expect"])
        else:
            n_nm += 1
            fa += bool(done(got))
    log("clips: %d of %d with the PC's actions; words differ in %d; %d split, then joined (%.0f s)" % (
        same_n, len(clips), text_diff, split, time.time() - t0))
    log("board: commands %d / %d exact, near misses %d, false actions %d" % (exact, n_cmd, n_nm, fa))
    if same_n != len(clips):   # (a split is the demo's own: a fragment then the whole; its actions are compared)
        fails.append("clips")
    if fa:
        fails.append("false actions")
    return fails


b = board.ready(log)
fails = []
for demo in sys.argv[1:] or ORDER:
    fails += ["%s: %s" % (demo, f) for f in check(b, demo)]
log("failed: " + ", ".join(fails) if fails else "all checks passed")
b.close()
log("FAIL" if fails else "PASS")
