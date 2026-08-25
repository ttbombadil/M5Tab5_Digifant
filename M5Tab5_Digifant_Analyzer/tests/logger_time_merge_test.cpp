#include "../src/logger_time_merge.h"

#include <cassert>

using namespace digifant::logging;

static LoggerCommand command(LoggerCommandKind kind, uint64_t timestamp) {
  return LoggerCommand{kind, timestamp};
}

int main() {
  LoggerTimeMerge merge;
  MergeItem item{};
  digifant::imu::ImuSample imu{};
  imu.timestampUs = 200;
  digifant::ui::MeasurementSnapshot snapshot{};
  snapshot.lastTimestampUs = 200;
  assert(merge.offerImu(imu));
  assert(merge.offerSnapshot(snapshot));
  assert(merge.offerCommand(command(LoggerCommandKind::Start, 100)));
  assert(merge.pop(item) && item.kind == MergeItemKind::Start);
  assert(merge.pop(item) && item.kind == MergeItemKind::Snapshot);
  assert(merge.pop(item) && item.kind == MergeItemKind::ImuSample);

  assert(merge.offerCommand(command(LoggerCommandKind::Marker, 300)));
  imu.timestampUs = 300;
  assert(merge.offerImu(imu));
  snapshot.lastTimestampUs = 300;
  assert(merge.offerSnapshot(snapshot));
  assert(merge.pop(item) && item.kind == MergeItemKind::Marker);
  assert(merge.pop(item) && item.kind == MergeItemKind::Snapshot);
  assert(merge.pop(item) && item.kind == MergeItemKind::ImuSample);

  assert(merge.offerCommand(command(LoggerCommandKind::Stop, 400)));
  imu.timestampUs = 401;
  assert(merge.offerImu(imu));
  assert(merge.pop(item) && item.kind == MergeItemKind::Stop);
  assert(!merge.pop(item));  // post-STOP pending records are discarded

  LoggerTimeMerge beforeStart;
  imu.timestampUs = 1;
  assert(beforeStart.offerImu(imu));
  assert(beforeStart.offerCommand(command(LoggerCommandKind::Start, 2)));
  assert(beforeStart.pop(item) && item.kind == MergeItemKind::Start);
  assert(!beforeStart.pop(item));

  LoggerTimeMerge holdback;
  assert(holdback.offerCommand(command(LoggerCommandKind::Start, 1000)));
  assert(holdback.pop(item) && item.kind == MergeItemKind::Start);
  imu.timestampUs = 2000;
  snapshot.lastTimestampUs = 1940;
  assert(holdback.offerImu(imu));
  assert(holdback.offerSnapshot(snapshot));
  assert(!holdback.pop(item, 6000));
  assert(holdback.pop(item, 30000) && item.kind == MergeItemKind::Snapshot);
  assert(holdback.pop(item, 30000) && item.kind == MergeItemKind::ImuSample);
  return 0;
}
