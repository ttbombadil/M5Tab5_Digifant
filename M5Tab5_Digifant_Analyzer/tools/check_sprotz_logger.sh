#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
ino="$root/M5Tab5_Digifant_Analyzer.ino"
shopt -s nullglob
runtime_sources=("$ino" "$root"/src/*.h "$root"/src/*.cpp)
logger_sources=("$root"/src/logger_*.h "$root"/src/sprotz_log*.h \
  "$root"/src/sprotz_logger*.h)

rg -q 'LoggerSnapshotQueue' "${runtime_sources[@]}"
rg -q 'LoggerCommandQueue' "${runtime_sources[@]}"
rg -q 'loggerSnapshots_\.trySend\(measurementModel_\.snapshot\(\)\)' \
  "$root/src/processing_service.h"
rg -q 'sprotz_logger_task|SprotzLoggerService' "${runtime_sources[@]}"

if rg -n 'RxIngress|KwpFrameEnvelope|DiagnosticDecoder|MeasurementModel|EspUsbHost' \
  "${logger_sources[@]}"; then
  echo "logger bypasses MeasurementSnapshot boundary" >&2
  exit 1
fi

sd_users="$(rg -l 'SD_MMC|fs::File' "$root/src" "$ino")"
test "$sd_users" = "$root/src/sprotz_logger_target.h"

if rg -n '\bSerial\.(print|printf|println|read|available|flush|begin)' \
  "${logger_sources[@]}"; then
  echo "logger writes Serial directly" >&2
  exit 1
fi

echo "SPROTZ logger: snapshot-only bounded writer path detected"
