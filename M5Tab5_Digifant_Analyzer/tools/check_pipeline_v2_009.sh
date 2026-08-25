#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
ino="$root/M5Tab5_Digifant_Analyzer.ino"
shopt -s nullglob
runtime_sources=("$ino" "$root"/src/*.h "$root"/src/*.cpp)

if rg -n '\bwire_rx(_count)?\b' "${runtime_sources[@]}"; then
  echo "V2-009: productive wire_rx path is still present" >&2
  exit 1
fi

rg -q 'rx_ingress\.publishBatch' "${runtime_sources[@]}"
rg -q 'rx_ingress\.tryPop' "${runtime_sources[@]}"
echo "V2-009: single RxIngressRing production boundary detected"
