# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""get_librispeech.py: a LibriSpeech test set from openslr.org, as transcribe reads it.

    python tools/eval/get_librispeech.py [--set test-clean] [--out data/<set>]

test-clean: 2620 clips, 5.4 h (a 346 MB download, 1.2 GB of .f32); test-other: 2939 clips, 5.3 h. Writes <id>.f32
(16 kHz mono, raw float32 little-endian) for each clip and refs.json ({id: text}). Needs numpy and soundfile (pip
install numpy soundfile).
"""
import argparse
import io
import json
import os
import tarfile
import urllib.request

import numpy as np
import soundfile as sf

MIRRORS = ["https://www.openslr.org/resources/12/", "https://openslr.elda.org/resources/12/"]
HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, "..", "..", "data"))


def open_any(urls):
    for u in urls:
        try:
            print("reading", u)
            return urllib.request.urlopen(u, timeout=60)
        except OSError as e:
            print("  ", e)
    raise SystemExit("no mirror answered")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", default="test-clean", choices=["test-clean", "test-other"])
    ap.add_argument("--out", help="default: data/<set>")
    ap.add_argument("--url", help="another mirror")
    a = ap.parse_args()
    a.out = a.out or os.path.join(DATA, a.set)
    os.makedirs(a.out, exist_ok=True)
    refs, n, sec = {}, 0, 0.0
    urls = [a.url] if a.url else [m + a.set + ".tar.gz" for m in MIRRORS]
    with open_any(urls) as r, tarfile.open(fileobj=r, mode="r|gz") as tar:
        for m in tar:
            if not m.isfile():
                continue
            name = os.path.basename(m.name)
            data = tar.extractfile(m).read()
            if name.endswith(".trans.txt"):
                for line in data.decode("utf-8").splitlines():
                    k, text = line.split(" ", 1)
                    refs[k] = text
            elif name.endswith(".flac"):
                x, sr = sf.read(io.BytesIO(data), dtype="float32")
                assert sr == 16000 and x.ndim == 1, name
                x.astype("<f4").tofile(os.path.join(a.out, name[:-5] + ".f32"))
                n += 1
                sec += len(x) / sr
                if n % 100 == 0:
                    print("\r  %d clips" % n, end="", flush=True)
    print()
    refs = {k: refs[k] for k in sorted(refs)}
    json.dump(refs, open(os.path.join(a.out, "refs.json"), "w", encoding="utf-8"), indent=0)
    print("%d clips (%.2f h), %d texts in %s" % (n, sec / 3600, len(refs), a.out))


if __name__ == "__main__":
    main()
