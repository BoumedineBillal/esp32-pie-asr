# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""download.py: the model's weights from Hugging Face, checked, and optionally copied to the SD card.

    python models/download.py                    models/kroko-en-q24-v1.bin (skipped when it is there and right)
    python models/download.py --card E:\\         the same, then copied to the SD card's top folder and read back

The firmware reads the file from the card's top folder at boot and refuses another size or CRC-32 (app.hpp).
Needs Python 3.8 or newer, nothing else.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import urllib.request
import zlib

REPO = "BoumedineBillal/esp32-pie-asr-kroko-en"
REVISION = "d97c28a8e35790899e391cce9399829f319dd034"   # the upload of kroko-en-q24-v1
FILE = "kroko-en-q24-v1.bin"
BYTES = 26651392
CRC = 0xbd3ff13c
SHA256 = "52673e74be9b44948640c6652ce8fd5b2b1b1faa4a66f3f8e43105405a38ce9f"

HERE = os.path.dirname(os.path.abspath(__file__))


def url(name, revision):
    return "https://huggingface.co/%s/resolve/%s/%s" % (REPO, revision, name)


def crc32(path, chunk=1 << 20):
    c, n = 0, 0
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                return n, c
            c = zlib.crc32(b, c)
            n += len(b)


def right(path):
    return os.path.isfile(path) and crc32(path) == (BYTES, CRC)


def download(dst, revision):
    # the model's description first: it names the file and its checks (and it is what the Hub counts downloads by)
    with urllib.request.urlopen(url("config.json", revision), timeout=60) as r:
        cfg = json.load(r)
    if (cfg.get("file"), cfg.get("bytes"), cfg.get("crc32"), cfg.get("sha256")) != (FILE, BYTES, "%08x" % CRC, SHA256):
        sys.exit("config.json at revision %s describes another file: %s" % (revision, cfg))
    print("downloading", url(FILE, revision))
    tmp = dst + ".part"
    c, n, h = 0, 0, hashlib.sha256()
    with urllib.request.urlopen(url(FILE, revision), timeout=60) as r, open(tmp, "wb") as f:
        while True:
            b = r.read(1 << 20)
            if not b:
                break
            f.write(b)
            c = zlib.crc32(b, c)
            h.update(b)
            n += len(b)
            print("\r  %5.1f / %.1f MB" % (n / 1e6, BYTES / 1e6), end="", flush=True)
    print()
    if (n, c, h.hexdigest()) != (BYTES, CRC, SHA256):
        os.remove(tmp)
        sys.exit("wrong file: %d bytes, CRC %08x (expected %d bytes, CRC %08x)" % (n, c, BYTES, CRC))
    os.replace(tmp, dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--card", help="the SD card's top folder (e.g. E:\\ or /media/sd): the file is copied there")
    ap.add_argument("--out", default=HERE, help="where the file is kept (default: this folder)")
    ap.add_argument("--revision", default=REVISION, help="the Hugging Face revision")
    a = ap.parse_args()

    path = os.path.join(a.out, FILE)
    if right(path):
        print(path, "is there and right")
    else:
        os.makedirs(a.out, exist_ok=True)
        download(path, a.revision)
        print("saved", path)
    if a.card:
        dst = os.path.join(a.card, FILE)
        print("copying to", dst)
        shutil.copyfile(path, dst)
        if not right(dst):
            sys.exit("the copy on the card reads back wrong: try again, or another card")
        print("the card has the model: put it in the board and restart it")


if __name__ == "__main__":
    main()
