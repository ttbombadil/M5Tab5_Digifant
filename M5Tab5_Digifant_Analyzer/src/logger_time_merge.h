#pragma once

#include "imu_sampler.h"
#include "logger_types.h"
#include "measurement_snapshot_types.h"

#include <cstdint>

namespace digifant::logging {

enum class MergeItemKind : uint8_t { Start, Marker, Snapshot, ImuSample, Stop };

struct MergeItem {
  MergeItemKind kind = MergeItemKind::Snapshot;
  uint64_t timestampUs = 0;
  ui::MeasurementSnapshot snapshot{};
  imu::ImuSample imu{};
  LoggerCommand command{};
};

class LoggerTimeMerge {
 public:
  bool offerSnapshot(const ui::MeasurementSnapshot& snapshot) noexcept {
    if (snapshotPending_) return false;
    snapshotPending_ = true;
    snapshot_ = snapshot;
    return true;
  }

  bool offerImu(const imu::ImuSample& sample) noexcept {
    if (imuPending_) return false;
    imuPending_ = true;
    imu_ = sample;
    return true;
  }

  bool offerCommand(const LoggerCommand& command) noexcept {
    if (commandPending_) return false;
    commandPending_ = true;
    command_ = command;
    return true;
  }

  bool hasSnapshotPending() const noexcept { return snapshotPending_; }
  bool hasImuPending() const noexcept { return imuPending_; }
  bool hasCommandPending() const noexcept { return commandPending_; }

  // The logger task supplies its current timestamp so a just-arrived source
  // cannot be emitted ahead of a producer that is only a few scheduler ticks
  // late.  The zero default preserves the deterministic unit-test behavior.
  bool pop(MergeItem& item, uint64_t nowUs = 0) noexcept {
    for (;;) {
      Candidate candidate{};
      if (!choose(candidate)) return false;
      if (nowUs != 0 && recording_ && candidate.timestampUs + kReorderHoldbackUs > nowUs)
        return false;
      if (recording_ && startTimestampUs_ != 0 && candidate.kind != MergeItemKind::Marker &&
          candidate.kind != MergeItemKind::Stop && candidate.timestampUs < startTimestampUs_) {
        discard(candidate);
        continue;
      }
      if (!recording_ && candidate.kind != MergeItemKind::Start) {
        discard(candidate);
        continue;
      }
      item = materialize(candidate);
      discard(candidate);
      if (item.kind == MergeItemKind::Start) {
        recording_ = true;
        startTimestampUs_ = item.timestampUs;
      }
      if (item.kind == MergeItemKind::Stop) recording_ = false;
      return true;
    }
  }

 private:
  struct Candidate {
    MergeItemKind kind = MergeItemKind::Snapshot;
    uint64_t timestampUs = 0;
  };

  static constexpr uint64_t kReorderHoldbackUs = 25'000;

  static uint8_t priority(MergeItemKind kind) noexcept {
    switch (kind) {
      case MergeItemKind::Start: return 0;
      case MergeItemKind::Marker: return 1;
      case MergeItemKind::Snapshot: return 2;
      case MergeItemKind::ImuSample: return 3;
      case MergeItemKind::Stop: return 4;
    }
    return 5;
  }

  bool choose(Candidate& result) const noexcept {
    bool found = false;
    const auto consider = [&result, &found](MergeItemKind kind, uint64_t timestamp) {
      if (!found || timestamp < result.timestampUs ||
          (timestamp == result.timestampUs && priority(kind) < priority(result.kind))) {
        result = {kind, timestamp};
        found = true;
      }
    };
    if (commandPending_) {
      MergeItemKind kind = MergeItemKind::Marker;
      if (command_.kind == LoggerCommandKind::Start) kind = MergeItemKind::Start;
      if (command_.kind == LoggerCommandKind::Stop) kind = MergeItemKind::Stop;
      consider(kind, command_.timestampUs);
    }
    if (snapshotPending_) consider(MergeItemKind::Snapshot, snapshot_.lastTimestampUs);
    if (imuPending_) consider(MergeItemKind::ImuSample, imu_.timestampUs);
    return found;
  }

  MergeItem materialize(const Candidate& candidate) const noexcept {
    MergeItem result{};
    result.kind = candidate.kind;
    result.timestampUs = candidate.timestampUs;
    if (candidate.kind == MergeItemKind::Snapshot) result.snapshot = snapshot_;
    if (candidate.kind == MergeItemKind::ImuSample) result.imu = imu_;
    if (candidate.kind == MergeItemKind::Start || candidate.kind == MergeItemKind::Marker ||
        candidate.kind == MergeItemKind::Stop) result.command = command_;
    return result;
  }

  void discard(const Candidate& candidate) noexcept {
    if (candidate.kind == MergeItemKind::Snapshot) snapshotPending_ = false;
    if (candidate.kind == MergeItemKind::ImuSample) imuPending_ = false;
    if (candidate.kind == MergeItemKind::Start || candidate.kind == MergeItemKind::Marker ||
        candidate.kind == MergeItemKind::Stop) commandPending_ = false;
  }

  bool recording_ = false;
  uint64_t startTimestampUs_ = 0;
  bool snapshotPending_ = false;
  bool imuPending_ = false;
  bool commandPending_ = false;
  ui::MeasurementSnapshot snapshot_{};
  imu::ImuSample imu_{};
  LoggerCommand command_{};
};

}  // namespace digifant::logging
