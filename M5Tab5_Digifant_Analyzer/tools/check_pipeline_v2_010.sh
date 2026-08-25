#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
ino="$root/M5Tab5_Digifant_Analyzer.ino"
shopt -s nullglob
runtime_sources=("$ino" "$root"/src/*.h "$root"/src/*.cpp)
if rg -n '\bPersistenceQueue\b|capture_queue' "${runtime_sources[@]}"; then
  echo "V2-010: second productive frame queue is still present" >&2
  exit 1
fi
rg -q 'ValidatedFrameQueue' "${runtime_sources[@]}"
rg -q 'tryReceive' "${runtime_sources[@]}"
echo "V2-010: single ValidatedFrameQueue production boundary detected"
