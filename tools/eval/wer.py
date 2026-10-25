# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""wer.py: the word error rate of transcribe's outputs against LibriSpeech's texts, and the texts that differ between
runs.

    python tools/eval/wer.py --refs data/test-clean/refs.json run_a.tsv [run_b.tsv ...]

A run is transcribe's output: one line per clip, "<clip path>\t<seconds> s\t<text>". The texts are compared after
a normalization (--norm):
    whisper   Whisper's English text normalizer (pip install whisper-normalizer), as ASR leaderboards score
    plain     upper case, no punctuation, hyphens as spaces (LibriSpeech's own form; nothing to install)
"""
import argparse
import json
import os
import re


def plain(s):
    s = s.upper().replace("-", " ")
    return [w.strip("'") for w in re.sub(r"[^A-Z' ]", " ", s).split() if w.strip("'")]


def whisper():
    from whisper_normalizer.english import EnglishTextNormalizer
    norm = EnglishTextNormalizer()
    return lambda s: norm(s).split()


def edits(r, h):
    d = list(range(len(h) + 1))
    for i in range(1, len(r) + 1):
        p, d[0] = d[:], i
        for j in range(1, len(h) + 1):
            d[j] = min(p[j] + 1, d[j - 1] + 1, p[j - 1] + (r[i - 1] != h[j - 1]))
    return d[len(h)]


def read_run(path):
    t = {}
    for line in open(path, encoding="utf-8"):
        f, _, txt = line.rstrip("\r\n").split("\t")
        t[os.path.splitext(os.path.basename(f))[0]] = txt
    return t


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--refs", required=True, help="refs.json from get_librispeech.py")
    ap.add_argument("--norm", choices=["whisper", "plain"], default="whisper")
    ap.add_argument("--show", type=int, default=10, help="texts shown per pair of runs that differ")
    ap.add_argument("runs", nargs="+")
    a = ap.parse_args()
    if a.norm == "whisper":
        try:
            words = whisper()
        except ImportError:
            raise SystemExit("--norm whisper needs: pip install whisper-normalizer (or use --norm plain)")
    else:
        words = plain
    print("normalization:", a.norm)
    refs = json.load(open(a.refs, encoding="utf-8"))
    runs = {p: read_run(p) for p in a.runs}
    for p, t in runs.items():
        missing = [i for i in refs if i not in t]
        ids = [i for i in refs if i in t]
        E = sum(edits(words(refs[i]), words(t[i])) for i in ids)
        N = sum(len(words(refs[i])) for i in ids)
        print("%-40s WER %.3f %% (%d / %d words), %d clips%s" % (os.path.basename(p), 100.0 * E / N, E, N, len(ids),
              ", %d missing" % len(missing) if missing else ""))
    names = list(runs)
    for x in range(len(names)):
        for y in range(x + 1, len(names)):
            A, B = runs[names[x]], runs[names[y]]
            diff = [i for i in refs if i in A and i in B and A[i] != B[i]]
            print("%s vs %s: %d texts differ" % (os.path.basename(names[x]), os.path.basename(names[y]), len(diff)))
            for i in diff[:a.show]:
                print("  %s\n    A: %s\n    B: %s" % (i, A[i], B[i]))


if __name__ == "__main__":
    main()
