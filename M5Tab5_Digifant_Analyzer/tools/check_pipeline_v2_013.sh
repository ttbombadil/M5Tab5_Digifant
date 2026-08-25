#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
shopt -s nullglob
runtime_sources=("$root/M5Tab5_Digifant_Analyzer.ino" "$root"/src/*.h "$root"/src/*.cpp)
portable_sources=("$root"/src/*.h "$root"/src/*.cpp)
for source in "${portable_sources[@]}"; do
  case "$(basename "$source")" in
    serial_consumer.h|serial_consumer.cpp) continue ;;
  esac
  if rg -n '\bSerial\.(print|printf|println|read|available|flush|begin)' "$source"; then
    echo "V2-013: Serial leaked into portable processing/decoder code" >&2
    exit 1
  fi
done
if ! rg -q 'snapshot_fanout\.serial|\.serial\(\)\.receive' "${runtime_sources[@]}"; then
  echo "V2-013: Serial leaked into portable processing/decoder code" >&2
  exit 1
fi
rg -q 'serial_snapshot_task|serial.consumer|serial_consumer' "${runtime_sources[@]}"
echo "V2-013: Serial is a snapshot-only consumer"
