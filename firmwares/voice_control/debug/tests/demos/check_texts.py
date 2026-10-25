# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""check_texts.py [demo...]: texts.py's sentences through the rules as written (build/voice_rules/voice_rules_cli,
Linux): each command gives its expected actions, each near miss none to do. Run before making clips."""
import os
import subprocess
import sys

from texts import DEMOS

HERE = os.path.dirname(os.path.abspath(__file__))
CLI = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "..", "build", "voice_rules", "voice_rules_cli"))


def done(actions):
    return [a for a in actions.split(" (")[0].split("; ") if a and not a.startswith("-") and not a.startswith("(")]


bad = 0
for demo in sys.argv[1:] or list(DEMOS):
    commands, near = DEMOS[demo]
    texts = [t for t, _ in commands] + near
    out = subprocess.run([CLI, demo], input="\n".join(texts) + "\n", capture_output=True, text=True).stdout
    got = [line.split("\t", 1)[1] for line in out.strip().split("\n")]
    for (t, e), g in zip(commands, got):
        if g != e:
            print("%-10s %-70s got %s, expected %s" % (demo, t, g, e))
            bad += 1
    for t, g in zip(near, got[len(commands):]):
        if done(g):
            print("%-10s near miss %-60s gives %s" % (demo, t, g))
            bad += 1
    print("%s: %d commands, %d near misses" % (demo, len(commands), len(near)))
print("all as expected" if not bad else "%d differ" % bad)
