# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""score.py <demo> <actions.tsv>...: each file's actions (voice_rules_cli: "clip.f32<TAB>actions") against
<demo>/set.json: the commands that gave exactly their expected actions (texts.SAME's other names of the same action
allowed), and the near misses that gave an action to do (a false action); every miss with its text as recognized
(from the matching pc_*.tsv, if there)."""
import json
import os
import sys

from texts import same

HERE = os.path.dirname(os.path.abspath(__file__))


def load(path):
    out = {}
    for line in open(path, encoding="utf-8"):
        k, v = (line.rstrip("\n").split("\t", 1) + [""])[:2]
        out[os.path.basename(k).rsplit(".", 1)[0]] = v
    return out


def done(actions):
    """the actions to do (not skipped) in a describe() string"""
    return [a for a in actions.split(" (")[0].split("; ") if a and not a.startswith("-") and not a.startswith("(")]


def main():
    demo = sys.argv[1]
    clips = json.load(open(os.path.join(HERE, demo, "set.json"), encoding="utf-8"))["clips"]
    for path in sys.argv[2:]:
        got = load(path)
        texts_path = path.replace("_actions.tsv", ".tsv")
        texts = {}
        if os.path.exists(texts_path):
            for line in open(texts_path, encoding="utf-8"):
                p = line.rstrip("\n").split("\t")
                texts[p[0].rsplit(".", 1)[0]] = p[-1]
        ok = n = fa = nm = 0
        misses = []
        for c in clips:
            a = got.get(c["id"], "(missing)")
            if c["group"] == "command":
                n += 1
                if same(demo, a) == same(demo, c["expect"]):
                    ok += 1
                else:
                    misses.append((c["id"], c["text"], texts.get(c["id"], "?"), a, c["expect"]))
            else:
                nm += 1
                if done(a):
                    fa += 1
                    misses.append((c["id"], c["text"], texts.get(c["id"], "?"), a, "no action"))
        print("%s %s: commands %d / %d exact (%.1f %%), near misses %d, false actions %d" %
              (demo, os.path.basename(path), ok, n, 100.0 * ok / max(n, 1), nm, fa))
        for cid, text, heard, a, e in misses:
            print("   %-24s said   %s\n   %-24s heard  %s\n   %-24s got    %s\n   %-24s expect %s" %
                  (cid, text, "", heard, "", a, "", e))


if __name__ == "__main__":
    main()
