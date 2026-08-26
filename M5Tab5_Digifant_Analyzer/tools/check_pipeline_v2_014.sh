#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
shopt -s nullglob
ui_sources=("$root"/src/display_ui*.h "$root"/src/display_ui*.cpp \
  "$root/src/measurement_snapshot.h")
if rg -n 'EspUsbHost|kwp1281_core|kwp_measurement_session|diagnostic_decoder' \
  "${ui_sources[@]}"; then
  echo "V2-014: UI headers must not depend on transport/KWP/decoder" >&2
  exit 1
fi
runtime_sources=("$root/M5Tab5_Digifant_Analyzer.ino" "$root"/src/*.h "$root"/src/*.cpp)
rg -q 'snapshot_fanout\.display|\.display\(\)\.receive' "${runtime_sources[@]}"
rg -q 'display_snapshot_task|display_consumer' "${runtime_sources[@]}"
echo "V2-014: Display is a snapshot-only consumer"
