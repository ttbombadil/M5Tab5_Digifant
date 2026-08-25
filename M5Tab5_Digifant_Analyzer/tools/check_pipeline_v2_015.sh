#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
ino="$root/M5Tab5_Digifant_Analyzer.ino"
shopt -s nullglob
runtime_sources=("$ino" "$root"/src/*.h "$root"/src/*.cpp)

test "$(rg -l '^digifant::transport::RxIngressRing [A-Za-z_]+;' "${runtime_sources[@]}" | wc -l | tr -d ' ')" = 1
test "$(rg -l '^digifant::transport::ValidatedFrameQueue [A-Za-z_]+;' "${runtime_sources[@]}" | wc -l | tr -d ' ')" = 1
test "$(rg -l 'diagnostic::DiagnosticDecoder [A-Za-z_]+_;' "${runtime_sources[@]}" | wc -l | tr -d ' ')" = 1
rg -q 'ProcessingService processing_service' "$ino"

if rg -n '\bwire_rx(_count)?\b|PersistenceQueue|RawFrameRecord|raw_capture_queue|setCaptureSink' \
  "${runtime_sources[@]}"; then
  echo "V2-015: obsolete parallel runtime path remains" >&2
  exit 1
fi

for consumer in serial display bluetooth web; do
  rg -q "snapshot_fanout\\.${consumer}|\\.${consumer}\\(\\)\\.receive" "${runtime_sources[@]}"
done
rg -q 'snapshots_\.publish\(measurementModel_\.snapshot\(\)\)' \
  "$root/src/processing_service.h"

if rg -n '\bSerial\.(print|printf|println|read|available|flush|begin)' "$root"/src/*.h "$root"/src/*.cpp \
  -g '!serial_consumer.h' -g '!serial_consumer.cpp' 2>/dev/null; then
  echo "V2-015: a portable upstream/display component accesses Serial" >&2
  exit 1
fi

# Serial.begin/flush are bounded startup operations. Runtime formatting must
# occur only in the dedicated low-priority snapshot consumer. The `.ino` check
# remains as a guard until R6 moves the consumer into its own source file.
outside_serial="$({
  awk '
    /void serial_snapshot_task_entry\(void\*\)/ { in_consumer=1 }
    in_consumer && /^}/ { in_consumer=0; next }
    !in_consumer && /Serial\.(print|printf|println)/ { print NR ":" $0 }
  ' "$ino"
} || true)"
if [[ -n "$outside_serial" ]]; then
  echo "$outside_serial" >&2
  echo "V2-015: runtime Serial output bypasses the snapshot consumer" >&2
  exit 1
fi

"$root/tools/check_pipeline_v2_009.sh"
"$root/tools/check_pipeline_v2_010.sh"
"$root/tools/check_pipeline_v2_013.sh"
"$root/tools/check_pipeline_v2_014.sh"
echo "V2-015: single pipeline and four independent snapshot consumers detected"
