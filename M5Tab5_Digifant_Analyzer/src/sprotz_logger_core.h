#pragma once

#include "sprotz_log_format.h"

namespace digifant::logging {

class SprotzLoggerCore {
 public:
  explicit SprotzLoggerCore(LogSink& sink, bool v2 = false) noexcept
      : sink_(sink), v2_(v2) { initializeCoordinates(latest_); }

  void storageReady(bool ready) noexcept {
    status_.state = ready ? LoggerState::Ready : LoggerState::NoStorage;
    status_.lastError = ready ? LoggerError::None : LoggerError::Open;
  }

  void acceptSnapshot(const ui::MeasurementSnapshot& snapshot) noexcept {
    latest_ = snapshot;
    if (status_.state != LoggerState::Recording) return;
    const uint64_t at = snapshot.lastTimestampUs;
    if ((v2_ ? writeV2Snapshot(at) : writeRecord(LogRecordKind::Snapshot, at)))
      ++status_.snapshotsWritten;
  }

  void handle(const LoggerCommand& command) noexcept {
    switch (command.kind) {
      case LoggerCommandKind::Start: start(command.timestampUs); break;
      case LoggerCommandKind::Stop:
        if (status_.state == LoggerState::Recording) {
          if ((v2_ ? writeV2(V2RecordKind::Stop, command.timestampUs, nullptr, 0)
                  : writeRecord(LogRecordKind::Stop, command.timestampUs))) ++status_.eventsWritten;
          (void)sink_.flush();
          sink_.close();
          if (status_.state == LoggerState::Recording) status_.state = LoggerState::Ready;
        }
        break;
      case LoggerCommandKind::Marker:
        if (status_.state == LoggerState::Recording &&
            (v2_ ? writeV2(V2RecordKind::Marker, command.timestampUs, nullptr, 0)
                 : writeRecord(LogRecordKind::Marker, command.timestampUs))) {
          ++status_.eventsWritten;
          status_.lastEventAtUs = command.timestampUs;
          (void)sink_.flush();
        }
        break;
    }
  }

  bool flush() noexcept {
    if (status_.state != LoggerState::Recording) return true;
    if (sink_.flush()) return true;
    fail(LoggerState::WriteError, LoggerError::Write);
    return false;
  }

  void noteQueueDrops(uint32_t snapshotDrops, uint32_t imuDrops,
                      uint32_t commandDrops) noexcept {
    status_.queueDrops = snapshotDrops;
    status_.snapshotQueueDrops = snapshotDrops;
    status_.imuQueueDrops = imuDrops;
    status_.commandQueueDrops = commandDrops;
    if (snapshotDrops != 0 && status_.state == LoggerState::Recording)
      status_.lastError = LoggerError::SnapshotQueueFull;
  }

  void acceptImu(const imu::ImuSample& sample) noexcept {
    if (status_.state == LoggerState::Recording && v2_) {
      const auto payload = BinaryLogV2Format::imuPayload(sample);
      if (!writeV2(V2RecordKind::ImuSample, sample.timestampUs, payload.data(), payload.size())) return;
    }
    ++status_.imuSamplesMerged;
  }

  void noteImuMerged() noexcept { ++status_.imuSamplesMerged; }

  const LoggerStatus& status() const noexcept { return status_; }

 private:
  void start(uint64_t timestampUs) noexcept {
    if (status_.state == LoggerState::Recording) return;
    if (!sink_.open(timestampUs, latest_.sessionEpoch, latest_.transportGeneration)) {
      fail(LoggerState::NoStorage, LoggerError::Open);
      return;
    }
    status_ = {};
    status_.state = LoggerState::Recording;
    status_.startedAtUs = timestampUs;
    copyFileName(sink_.fileName());
    if (v2_) {
      const auto fileHeader = BinaryLogV2Format::header(timestampUs);
      if (!writeBytes(fileHeader.data(), fileHeader.size())) return;
      const auto orientation = BinaryLogV2Format::orientationPayload();
      if (!writeV2(V2RecordKind::ImuOrientation, timestampUs, orientation.data(), orientation.size())) return;
    }
    const auto fileHeader = BinaryLogFormat::header(timestampUs);
    if (!v2_ && !writeBytes(fileHeader.data(), fileHeader.size())) return;
    if ((v2_ ? writeV2(V2RecordKind::Start, timestampUs, nullptr, 0)
             : writeRecord(LogRecordKind::Start, timestampUs))) {
      ++status_.eventsWritten;
      status_.lastEventAtUs = timestampUs;
    }
  }

  bool writeRecord(LogRecordKind kind, uint64_t timestampUs) noexcept {
    if (sink_.freeBytes() < BinaryLogFormat::kRecordSize) {
      fail(LoggerState::StorageFull, LoggerError::Full);
      return false;
    }
    const auto record = BinaryLogFormat::record(kind, timestampUs, latest_);
    return writeBytes(record.data(), record.size());
  }

  bool writeV2Snapshot(uint64_t timestampUs) noexcept {
    const auto& payload = BinaryLogFormat::record(LogRecordKind::Snapshot, timestampUs, latest_);
    return writeV2(V2RecordKind::EcuSnapshot, timestampUs, payload.data(), payload.size());
  }

  bool writeV2(V2RecordKind kind, uint64_t timestampUs, const uint8_t* payload,
               std::size_t payloadSize) noexcept {
    if (payloadSize > BinaryLogV2Format::kMaxPayloadSize ||
        sink_.freeBytes() < BinaryLogV2Format::kRecordHeaderSize + payloadSize) {
      fail(LoggerState::StorageFull, LoggerError::Full); return false;
    }
    std::size_t used = 0;
    const auto& record = BinaryLogV2Format::record(kind, timestampUs, payload, payloadSize, used);
    return writeBytes(record.data(), used);
  }

  bool writeBytes(const uint8_t* data, std::size_t size) noexcept {
    if (!sink_.write(data, size)) {
      fail(sink_.freeBytes() < size ? LoggerState::StorageFull : LoggerState::WriteError,
           sink_.freeBytes() < size ? LoggerError::Full : LoggerError::Write);
      return false;
    }
    status_.bytesWritten += size;
    return true;
  }

  void fail(LoggerState state, LoggerError error) noexcept {
    sink_.close();
    status_.state = state;
    status_.lastError = error;
  }

  void copyFileName(const char* value) noexcept {
    std::size_t i = 0;
    if (value != nullptr) {
      for (; i + 1U < status_.fileName.size() && value[i] != '\0'; ++i)
        status_.fileName[i] = value[i];
    }
    status_.fileName[i] = '\0';
  }

  static void initializeCoordinates(ui::MeasurementSnapshot& snapshot) noexcept {
    for (uint8_t i = 0; i < 10; ++i) {
      snapshot.fields[i].group = 0;
      snapshot.fields[i].zone = i;
    }
    for (uint8_t group = 1; group <= 4; ++group) {
      for (uint8_t zone = 1; zone <= 4; ++zone) {
        const uint8_t index = static_cast<uint8_t>(10U + (group - 1U) * 4U + zone - 1U);
        snapshot.fields[index].group = group;
        snapshot.fields[index].zone = zone;
      }
    }
  }

  LogSink& sink_;
  bool v2_ = false;
  LoggerStatus status_{};
  ui::MeasurementSnapshot latest_{};
};


}  // namespace digifant::logging
