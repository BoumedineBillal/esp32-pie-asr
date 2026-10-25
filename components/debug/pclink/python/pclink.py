# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0

"""pclink.py: the PC end of pclink (Windows, WinUSB): messages in, lines and bytes out.

    b = Link()
    b.start_pings()                # the board talks only while pinged
    b.cmd("hello")                 # the reply line ("ok ..."), RuntimeError on "err ..."
    b.cmd("load %d %d" % (n, crc), payload=data)   # a command that answers "ok send", then the bytes, then its reply
    magic, payload = b.message(timeout)
"""
import os
import struct
import sys
import threading
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(HERE, "..", "..", "usb_stream", "python"))
import winusb  # noqa: E402

MODEL = os.path.join(REPO, "models", "kroko-en-q24-v1.bin")   # models/download.py puts it there


class Link:
    def __init__(self, wait_s=15, on_text=None, on_message=None):
        self.dev = winusb.Device(wait_s=wait_s)
        self.buf = bytearray()
        self.lock = threading.RLock()
        self.on_text = on_text         # every text line the board sends, as it comes
        self.on_message = on_message   # every other message (magic, payload), also those cmd() and text() skip
        self.stop = threading.Event()
        while self.dev.read():
            pass

    def send(self, line):
        with self.lock:
            self.dev.write((line + "\n").encode())

    def write(self, data, chunk=1 << 20):
        with self.lock:
            for i in range(0, len(data), chunk):
                self.dev.write(bytes(data[i:i + chunk]))

    def start_pings(self, period=1.0):
        """a "ping" line now and every period seconds until close()"""
        def ping():
            while not self.stop.wait(period):
                try:
                    self.send("ping")
                except OSError:
                    return
        self.send("ping")
        threading.Thread(target=ping, daemon=True).start()

    def message(self, timeout=None):
        """(magic, payload), or (None, None) after timeout seconds without one"""
        end = None if timeout is None else time.time() + timeout
        while True:
            if len(self.buf) >= 8:
                n = struct.unpack_from("<I", self.buf, 4)[0]
                if len(self.buf) >= 8 + n:
                    magic, p = bytes(self.buf[:4]), bytes(self.buf[8:8 + n])
                    del self.buf[:8 + n]
                    if magic == b"TXT0" and self.on_text:
                        self.on_text(p.decode(errors="replace"))
                    elif magic != b"TXT0" and self.on_message:
                        self.on_message(magic, p)
                    return magic, p
            d = self.dev.read()
            if d:
                self.buf += d
            elif end is not None and time.time() > end:
                return None, None

    def text(self, timeout=30):
        """the next text line"""
        end = time.time() + timeout
        while time.time() < end:
            magic, p = self.message(max(0.1, end - time.time()))
            if magic == b"TXT0":
                return p.decode(errors="replace")
        raise TimeoutError("no reply from the board in %.0f s" % timeout)

    def cmd(self, line, payload=None, timeout=60, lines=None):
        """send a command; with a payload, wait for "ok send" and send it; return the final reply ("ok ..." or
        "err ..."); lines: a list that gets the other text lines before it"""
        if payload is None:
            self.send(line)
        else:
            with self.lock:   # nothing else sent between: the board takes all the bytes after "ok send" as the payload
                self.send(line)
                r = self.text(timeout)
                if r != "ok send":
                    raise RuntimeError("%s -> %s" % (line, r))
                self.write(payload)
        while True:
            r = self.text(timeout)
            if r.startswith("ok") or r.startswith("err"):
                if r.startswith("err"):
                    raise RuntimeError("%s -> %s" % (line, r))
                return r
            if lines is not None:
                lines.append(r)

    def ensure_model(self, path=MODEL, log=print):
        """the weights sent if the board waits for them (no file on its SD card); returns the board's state line"""
        r = self.cmd("hello")
        if "waiting for the model" not in r:
            return r
        blob = open(path, "rb").read()
        log("sending the model: %d bytes (%s)" % (len(blob), os.path.basename(path)))
        t0 = time.time()
        log(self.cmd("load %d %d" % (len(blob), zlib.crc32(blob)), payload=blob, timeout=180),
            "(%.1f s)" % (time.time() - t0))
        return self.cmd("hello")

    def close(self):
        self.stop.set()
        self.dev.close()
