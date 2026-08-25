#pragma once

#include "logger_channels.h"
#include "sprotz_logger_core.h"
#include "logger_time_merge.h"

#include <FS.h>
#include <SD_MMC.h>

#include <cstdio>

namespace digifant::logging {

class SdMmcLogSink final : public LogSink {
 public:
  bool cardPresent() noexcept {
    return SD_MMC.cardType() != CARD_NONE && SD_MMC.totalBytes() != 0 &&
           SD_MMC.exists("/sprotz");
  }

  bool begin() noexcept {
    if (mounted_) return true;
    if (!pinsConfigured_ && !SD_MMC.setPins(43, 44, 39, 40, 41, 42)) return false;
    pinsConfigured_ = true;
    mounted_ = SD_MMC.begin("/sdcard", false);
    if (!mounted_) return false;
    if (!cardPresent()) {
      SD_MMC.end();
      mounted_ = false;
      return false;
    }
    if (!SD_MMC.exists("/sprotz") && !SD_MMC.mkdir("/sprotz")) {
      SD_MMC.end();
      mounted_ = false;
      return false;
    }
    return true;
  }

  bool probe(bool allowUnmount) noexcept {
    if (allowUnmount) {
      // SD_MMC keeps a successful mount alive after physical card removal on
      // this target. Reopen the controller while idle so card presence is
      // observed from the hardware, not from a stale filesystem mount.
      if (mounted_) {
        SD_MMC.end();
        mounted_ = false;
      }
      return begin();
    }
    if (!mounted_) return false;
    if (!cardPresent()) {
      return false;
    }
    return true;
  }

  bool open(uint64_t startedAtUs, uint32_t sessionEpoch,
            uint32_t transportGeneration) noexcept override {
    if (!mounted_ && !begin()) return false;
    close();
    for (uint16_t suffix = 0; suffix < 1000; ++suffix) {
      snprintf(fileName_.data(), fileName_.size(), "/sprotz/g%lu_s%lu_%llu_%u.dlog",
               static_cast<unsigned long>(transportGeneration),
               static_cast<unsigned long>(sessionEpoch),
               static_cast<unsigned long long>(startedAtUs), static_cast<unsigned>(suffix));
      if (SD_MMC.exists(fileName_.data())) continue;
      file_ = SD_MMC.open(fileName_.data(), FILE_WRITE);
      return static_cast<bool>(file_);
    }
    return false;
  }

  bool write(const uint8_t* data, std::size_t size) noexcept override {
    return file_ && file_.write(data, size) == size;
  }

  bool flush() noexcept override {
    if (!file_) return false;
    file_.flush();
    return true;
  }

  void close() noexcept override {
    if (!file_) return;
    file_.flush();
    file_.close();
  }

  uint64_t freeBytes() const noexcept override {
    if (!mounted_) return 0;
    const uint64_t total = SD_MMC.totalBytes();
    const uint64_t used = SD_MMC.usedBytes();
    return total > used ? total - used : 0;
  }

  const char* fileName() const noexcept override { return fileName_.data(); }

 private:
  fs::File file_{};
  std::array<char, 64> fileName_{};
  bool mounted_ = false;
  bool pinsConfigured_ = false;
};

class SprotzLoggerService {
 public:
  SprotzLoggerService(LoggerSnapshotQueue& snapshots, LoggerCommandQueue& commands,
                      imu::ImuSampleRing& imuSamples, LoggerStatusFanout& statusMailbox) noexcept
      : snapshots_(snapshots), commands_(commands), imuSamples_(imuSamples),
        statusMailbox_(statusMailbox), core_(sink_, true) {}

  void begin() noexcept {
    storagePresent_ = sink_.begin();
    core_.storageReady(storagePresent_);
    publishStatus();
  }

  void poll(uint64_t nowUs) noexcept {
    refreshStorage(nowUs);
    fillPending();
    MergeItem item{};
    while (merge_.pop(item, nowUs)) {
      if (item.kind == MergeItemKind::Snapshot) {
        core_.acceptSnapshot(item.snapshot);
        snapshotPending_ = false;
      } else if (item.kind == MergeItemKind::ImuSample) {
        core_.acceptImu(item.imu);
        imuPending_ = false;
      } else {
        core_.handle(item.command);
      }
      snapshotPending_ = merge_.hasSnapshotPending();
      imuPending_ = merge_.hasImuPending();
      commandPending_ = merge_.hasCommandPending();
      fillPending();
    }
    // pop() may discard pre-START/post-STOP values without returning an item.
    // Refresh all producer flags in that case as well, otherwise a discarded
    // pending value would permanently stall that source.
    snapshotPending_ = merge_.hasSnapshotPending();
    imuPending_ = merge_.hasImuPending();
    commandPending_ = merge_.hasCommandPending();
    core_.noteQueueDrops(snapshots_.drops(), imuSamples_.drops(), commands_.drops());

    if (core_.status().state == LoggerState::Recording &&
        (core_.status().snapshotsWritten - lastFlushedSnapshots_ >= 16U ||
         nowUs - lastFlushAtUs_ >= 2000000ULL)) {
      if (core_.flush()) {
        lastFlushedSnapshots_ = core_.status().snapshotsWritten;
        lastFlushAtUs_ = nowUs;
      }
    }
    if (nowUs - lastStatusAtUs_ >= 5000000ULL || statusChanged()) {
      publishStatus();
      lastStatusAtUs_ = nowUs;
    }
  }

 private:
  void refreshStorage(uint64_t nowUs) noexcept {
    if (nowUs - lastStorageProbeAtUs_ < kStorageProbePeriodUs) return;
    lastStorageProbeAtUs_ = nowUs;
    const LoggerState state = core_.status().state;
    const bool recording = state == LoggerState::Recording;
    const bool ready = sink_.probe(!recording);
    storagePresent_ = ready;
    // Recovery is allowed from every non-recording error state. During a
    // recording, the logger remains the sole SD owner and a probe may not
    // unmount the medium or overwrite the Recording state.
    if (!recording && ready != (state == LoggerState::Ready)) core_.storageReady(ready);
  }

  static constexpr uint64_t kStorageProbePeriodUs = 2'000'000;

  bool statusChanged() const noexcept {
    const auto& status = core_.status();
    return status.state != lastPublishedState_ || status.lastError != lastPublishedError_ ||
           storagePresent_ != lastPublishedStoragePresent_ ||
           status.snapshotsWritten != lastPublishedSnapshots_ ||
           status.eventsWritten != lastPublishedEvents_ || status.queueDrops != lastPublishedDrops_ ||
           status.imuQueueDrops != lastPublishedImuDrops_ ||
           status.imuSamplesMerged != lastPublishedImuMerged_;
  }

  void publishStatus() noexcept {
    LoggerStatus status = core_.status();
    status.storagePresent = storagePresent_;
    statusMailbox_.publish(status);
    lastPublishedState_ = status.state;
    lastPublishedError_ = status.lastError;
    lastPublishedStoragePresent_ = status.storagePresent;
    lastPublishedSnapshots_ = status.snapshotsWritten;
    lastPublishedEvents_ = status.eventsWritten;
    lastPublishedDrops_ = status.queueDrops;
    lastPublishedImuDrops_ = status.imuQueueDrops;
    lastPublishedImuMerged_ = status.imuSamplesMerged;
  }

  void fillPending() noexcept {
    if (!snapshotPending_) {
      ui::MeasurementSnapshot snapshot{};
      if (snapshots_.tryReceive(snapshot)) snapshotPending_ = merge_.offerSnapshot(snapshot);
    }
    if (!imuPending_) {
      imu::ImuSample sample{};
      if (imuSamples_.tryPop(sample)) imuPending_ = merge_.offerImu(sample);
    }
    if (!commandPending_) {
      LoggerCommand command{};
      if (commands_.tryReceive(command)) commandPending_ = merge_.offerCommand(command);
    }
  }

  LoggerSnapshotQueue& snapshots_;
  LoggerCommandQueue& commands_;
  imu::ImuSampleRing& imuSamples_;
  LoggerStatusFanout& statusMailbox_;
  SdMmcLogSink sink_{};
  SprotzLoggerCore core_;
  uint64_t lastFlushAtUs_ = 0;
  uint64_t lastStatusAtUs_ = 0;
  uint64_t lastStorageProbeAtUs_ = 0;
  bool storagePresent_ = false;
  uint32_t lastFlushedSnapshots_ = 0;
  LoggerState lastPublishedState_ = LoggerState::Initializing;
  LoggerError lastPublishedError_ = LoggerError::None;
  bool lastPublishedStoragePresent_ = false;
  uint32_t lastPublishedSnapshots_ = 0;
  uint32_t lastPublishedEvents_ = 0;
  uint32_t lastPublishedDrops_ = 0;
  uint32_t lastPublishedImuDrops_ = 0;
  uint32_t lastPublishedImuMerged_ = 0;
  LoggerTimeMerge merge_{};
  bool snapshotPending_ = false;
  bool imuPending_ = false;
  bool commandPending_ = false;
};

}  // namespace digifant::logging
