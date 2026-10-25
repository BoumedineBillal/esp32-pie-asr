# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""phrases_check.py: the phrases on the board.

The 140 clips of phrases/ (make_clips.py: 35 sentences x 4 text-to-speech voices; 10 with the default list's words,
15 with its sentences, 10 with none but near-miss words) fed through the live path with all the default phrases on.
For each clip:
  - the board's pieces = the PC's (phrases/reference_phrases.tsv: the engine's host build with the same list);
  - the board's words (its transcript, with the rewrites) = the PC's pieces rewritten the same way (here, below).
Then the recall of the list's words and sentences (strict: the shown form; loose: letters and digits only), the false
alarms (a listed phrase in the output that was not said) and the WER, for the PC's beam 4 without the phrases (as
the board shows it with them off), and the board with them on. The phrases' switches are put back. Log:
logs/phrases_check.txt.
"""
import json
import os
import re

import board

P = os.path.join(board.HERE, "phrases")
DEFAULT = os.path.join(board.HERE, "..", "..", "phrases", "default.txt")   # the firmware's list
log = board.logger("phrases_check")

S = json.load(open(os.path.join(P, "set.json"), encoding="utf-8"))
LIST = S["words"] + S["sentences"]
REWRITE = []   # (spoken, shown), as the firmware reads the list
for line in open(DEFAULT, encoding="utf-8"):
    line = line.strip()
    if line and not line.startswith("#") and "=" in line:
        shown, rest = line.split("=", 1)
        for f in rest.split("|"):
            REWRITE.append((" ".join(f.split()), " ".join(shown.split())))


def load_ref(name):
    t = {}
    for line in open(os.path.join(P, name), encoding="utf-8"):
        f, sec, txt = line.rstrip("\n").split("\t")
        t[os.path.basename(f)[:-4]] = txt
    return t


def norm_word(w):
    return re.sub(r"[.,?!;:]+$", "", w.lower())


def rewrite(text):
    """the transcript's rewrite on whole words: at each word the longest spoken form starting there becomes its shown
    form (with the last word's trailing punctuation)"""
    words, out, i = text.split(), [], 0
    forms = [(f.split(), s) for f, s in REWRITE]
    while i < len(words):
        best = None
        for fw, shown in forms:
            n = len(fw)
            if [norm_word(w) for w in words[i:i + n]] == [x.lower() for x in fw] and (not best or n > best[0]):
                best = (n, shown)
        if best:
            last = words[i + best[0] - 1]
            out.append(best[1] + last[len(re.sub(r"[.,?!;:]+$", "", last)):])
            i += best[0]
        else:
            out.append(words[i])
            i += 1
    return " ".join(out)


def strict(s):
    s = re.sub(r"[.,?!;:\"()]", " ", s.lower())
    return " " + " ".join(s.split()) + " "


def loose(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


def wer_words(s):
    return re.sub(r"[^a-z0-9' ]", " ", s.lower().replace("-", " ")).split()


def edits(r, h):
    d = list(range(len(h) + 1))
    for i in range(1, len(r) + 1):
        p, d[0] = d[:], i
        for j in range(1, len(h) + 1):
            d[j] = min(p[j] + 1, d[j - 1] + 1, p[j - 1] + (r[i - 1] != h[j - 1]))
    return d[len(h)]


def score(hyp):
    r = {}
    for grp in ("word", "sentence"):
        n = hs = hl = 0
        for c in S["clips"]:
            if c["group"] != grp:
                continue
            h = hyp[c["id"]]
            for w in set(c["phrases"]):
                k = c["phrases"].count(w)
                n += k
                hs += min(k, strict(h).count(" " + strict(w).strip() + " "))
                hl += min(k, loose(h).count(loose(w)))
        r[grp] = (hs, hl, n)
    fa_none = fa_all = 0
    for c in S["clips"]:
        hs, rs = strict(hyp[c["id"]]), strict(c["text"])
        for w in sorted(LIST, key=len, reverse=True):
            extra = hs.count(" " + strict(w).strip() + " ") - rs.count(" " + strict(w).strip() + " ")
            if extra > 0:
                fa_all += extra
                fa_none += extra if c["group"] == "none" else 0
    r["fa"] = (fa_none, fa_all)
    E = N = 0
    for c in S["clips"]:
        rw = wer_words(c["text"])
        E += edits(rw, wer_words(hyp[c["id"]]))
        N += len(rw)
    r["wer"] = 100.0 * E / N
    return r


def show(name, r):
    (ws, wl, wn), (ss, sl, sn) = r["word"], r["sentence"]
    log("%-34s words %3d/%d %5.1f %% (loose %5.1f %%) | sentences %3d/%d %5.1f %% | false alarms %d (none: %d) | "
        "WER %.1f %%" % (name, ws, wn, 100.0 * ws / wn, 100.0 * wl / wn, ss, sn, 100.0 * ss / sn, r["fa"][1],
                         r["fa"][0], r["wer"]))


pc_off, pc_on = load_ref("reference_beam4.tsv"), load_ref("reference_phrases.tsv")
b = board.ready(log)
before = b.cmd("phrases")
log(before[:200])
was_on = before.startswith("ok phrases on")
items = re.findall(r" (\d+):(on|off):", before)
for i, st in items:
    if st == "off":
        b.cmd("phrase %s 1" % i)
log(b.cmd("phrases on")[:120])
b.cmd("uicheck reset")
board_text, same_pieces, same_words = {}, 0, 0
for c in S["clips"]:
    p = board.parse_feed(board.feed(b, board.clip(c["id"], os.path.join(P, "clips"))))
    text, raw = (p[3], p[4]) if p else ("?", "")
    board_text[c["id"]] = text
    pieces_ok = raw.replace("▁", " ").strip() == pc_on[c["id"]]
    words_ok = text == rewrite(pc_on[c["id"]])
    same_pieces += pieces_ok
    same_words += words_ok
    if not (pieces_ok and words_ok):
        log("DIFF %s: board %r | pieces %r | PC %r" % (c["id"], text, raw, pc_on[c["id"]]))
log("pieces: the board = the PC on %d of %d clips; words (with the rewrites) on %d of %d" % (
    same_pieces, len(S["clips"]), same_words, len(S["clips"])))
log(b.cmd("stats"))
log(b.cmd("uicheck"))
if not was_on:
    log(b.cmd("phrases off")[:60])
for i, st in items:
    if st == "off":
        b.cmd("phrase %s 0" % i)
b.close()
show("PC beam 4, phrases off", score(pc_off))
show("PC beam 4, phrases off, rewritten", score({k: rewrite(v) for k, v in pc_off.items()}))
show("the board, phrases on", score(board_text))
ok = same_pieces == len(S["clips"]) and same_words == len(S["clips"])
log("PASS" if ok else "FAIL")
