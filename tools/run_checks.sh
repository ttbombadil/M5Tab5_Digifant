#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

echo "== Audio-/Touch-Hosttests =="
python3 -m unittest discover -s M5Tab5_Audio_Probe/host_tests -v

echo "== Analyzer-DLOG-Test =="
python3 M5Tab5_Digifant_Analyzer/tests/dlog_v2_test.py

echo "== Analyzer-Guards =="
for check in M5Tab5_Digifant_Analyzer/tools/check_*.sh; do
    echo "-- $check"
    sh "$check"
done

if command -v arduino-cli >/dev/null 2>&1; then
    echo "== Arduino-Compile: Touch Probe =="
    arduino-cli compile --fqbn esp32:esp32:m5stack_tab5 \
        --build-path build_touch_only M5Tab5_Touch_Probe
else
    echo "arduino-cli nicht gefunden; Arduino-Compile übersprungen."
fi
