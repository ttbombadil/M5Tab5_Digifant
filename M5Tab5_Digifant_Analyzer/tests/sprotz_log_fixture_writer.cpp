#include "../src/sprotz_logger.h"

#include <cstdio>

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  auto snapshot = digifant::ui::MeasurementSnapshot{};
  snapshot.lastTimestampUs = 123456;
  snapshot.lastRxSequence = 7;
  snapshot.sessionEpoch = 2;
  snapshot.transportGeneration = 4;
  for (uint8_t i = 0; i < 10; ++i) {
    snapshot.fields[i].group = 0;
    snapshot.fields[i].zone = i;
    snapshot.fields[i].raw = static_cast<uint8_t>(10 + i);
  }
  for (uint8_t group = 1; group <= 4; ++group) {
    for (uint8_t zone = 1; zone <= 4; ++zone) {
      const uint8_t i = static_cast<uint8_t>(10U + (group - 1U) * 4U + zone - 1U);
      snapshot.fields[i].group = group;
      snapshot.fields[i].zone = zone;
      snapshot.fields[i].raw = i;
    }
  }
  const auto header = digifant::logging::BinaryLogFormat::header(100000);
  const auto record = digifant::logging::BinaryLogFormat::record(
      digifant::logging::LogRecordKind::Snapshot, 123456, snapshot);
  std::FILE* file = std::fopen(argv[1], "wb");
  if (file == nullptr) return 3;
  const bool ok = std::fwrite(header.data(), header.size(), 1, file) == 1 &&
                  std::fwrite(record.data(), record.size(), 1, file) == 1;
  return std::fclose(file) == 0 && ok ? 0 : 4;
}
