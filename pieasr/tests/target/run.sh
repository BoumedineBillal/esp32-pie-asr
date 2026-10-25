#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Boumedine Billal
# SPDX-License-Identifier: Apache-2.0
# run.sh [port]: the PIE kernels against their C references on the board. Builds and flashes this test firmware,
# records its console into logs/run.txt, prints each kernel's line and PASS when every one says "0 wrong". ESP-IDF's
# environment must be set (. $IDF_PATH/export.sh); the port defaults to /dev/ttyACM0.
cd "$(dirname "$0")" || exit 1
PORT=${1:-/dev/ttyACM0}
idf.py -D IDF_TARGET=esp32p4 build > /dev/null || { echo "FAIL: build"; exit 1; }
idf.py -p "$PORT" flash > /dev/null || { echo "FAIL: flash"; exit 1; }
mkdir -p logs
log=logs/run.txt
python - "$PORT" "$log" << 'EOF'
import sys, time, serial
port, out = sys.argv[1], sys.argv[2]
s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 115200, 0.5
s.dtr = s.rts = False   # opening the port must not reset the board again
for _ in range(40):     # the port comes back after the flash's reset
    try:
        s.open()
        break
    except serial.SerialException:
        time.sleep(0.5)
data, end = b"", time.time() + 60
while time.time() < end and b"kernels done" not in data:
    data += s.read(4096)
open(out, "wb").write(data)
EOF
grep -aE "wrong|cycles|kernels" "$log" | tr -d '\r'
n=$(grep -aoE "[0-9]+ wrong" "$log" | wc -l)
bad=$(grep -aoE "[0-9]+ wrong" "$log" | grep -vc "^0 wrong")
if grep -aq "kernels done" "$log" && [ "$n" -gt 0 ] && [ "$bad" = 0 ]; then
    echo "PASS: $n kernel counts, all 0 wrong"
else
    echo "FAIL: $n kernel counts, $bad not 0 (see $log)"
    exit 1
fi
