#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace digifant::logging {

enum class LoggerCommandKind : uint8_t {
  LogStart = 1,
  LogStop = 2,
  Marker = 3,
  SprotzStart = 4,
  SprotzStop = 5,
  // Compatibility aliases for the existing command producers/tests.
  Start = LogStart,
  Stop = LogStop
};
enum class LogRecordKind : uint8_t { Snapshot = 1, Start = 2, Stop = 3, Marker = 4 };
enum class V2RecordKind : uint8_t {
  EcuSnapshot = 1, ImuSample = 2, Start = 3, Stop = 4, Marker = 5, ImuOrientation = 6
};
enum class LoggerState : uint8_t {
  Initializing,
  Ready,
  Recording,
  NoStorage,
  StorageFull,
  WriteError
};
enum class LoggerError : uint8_t { None, CommandQueueFull, SnapshotQueueFull, Open, Write, Full };

struct LoggerCommand {
  LoggerCommandKind kind = LoggerCommandKind::Start;
  uint64_t timestampUs = 0;
};

struct LoggerStatus {
  LoggerState state = LoggerState::Initializing;
  LoggerError lastError = LoggerError::None;
  bool storagePresent = false;
  uint64_t startedAtUs = 0;
  uint64_t lastEventAtUs = 0;
  bool sprotzActive = false;
  uint32_t snapshotsWritten = 0;
  uint32_t eventsWritten = 0;
  uint32_t queueDrops = 0;
  uint32_t snapshotQueueDrops = 0;
  uint32_t imuQueueDrops = 0;
  uint32_t commandQueueDrops = 0;
  uint32_t imuSamplesMerged = 0;
  uint64_t bytesWritten = 0;
  std::array<char, 64> fileName{};
};

static_assert(std::is_trivially_copyable_v<LoggerCommand>);
static_assert(std::is_trivially_copyable_v<LoggerStatus>);

}  // namespace digifant::logging
