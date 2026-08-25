#pragma once

#include "imu_sample_ring.h"
#include "logger_channels.h"
#include "measurement_snapshot_mailbox.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace digifant::serial {

using NowProvider = uint64_t (*)(void* context) noexcept;
using StackFreeProvider = uint32_t (*)(void* context) noexcept;

// SerialConsumer owns only serial parsing/formatting state. The target's
// Serial object is supplied as a tiny compile-time I/O surface so the exact
// command and output behavior can be host-tested without Arduino headers.
template <typename SerialIo>
class SerialConsumer {
 public:
  SerialConsumer(logging::LoggerCommandQueue& commands,
                 logging::LoggerStatusMailbox& loggerStatus,
                 ui::LatestSnapshotMailbox& snapshots,
                 imu::ImuDiagnosticsMailbox& imuDiagnostics,
                 std::atomic<uint8_t>& tabRequest,
                 SerialIo& serial)
      : commands_(commands),
        loggerStatus_(loggerStatus),
        snapshots_(snapshots),
        imuDiagnostics_(imuDiagnostics),
        tabRequest_(tabRequest),
        serial_(serial) {}

  void poll(NowProvider nowProvider, void* nowContext,
            StackFreeProvider stackFreeProvider = nullptr,
            void* stackFreeContext = nullptr) noexcept {
    while (serial_.available() > 0) {
      processChar(static_cast<char>(serial_.read()), nowProvider, nowContext);
    }

    logging::LoggerStatus loggerStatus{};
    if (loggerStatus_.receive(loggerStatus)) {
      lastLoggerStatus = loggerStatus;
      haveLoggerStatus_ = true;
      printLoggerStatus(loggerStatus);
      if (stackFreeProvider != nullptr)
        serial_.printf("SPROTZ_STACK_FREE_WORDS=%lu\n",
                       static_cast<unsigned long>(stackFreeProvider(stackFreeContext)));
    }

    ui::MeasurementSnapshot snapshot{};
    if (snapshots_.receive(snapshot)) printSnapshot(snapshot);

    const uint32_t imuAccepted = imuDiagnostics_.accepted();
    if (imuAccepted - imuReported_ >= 125U) {
      imu::ImuSample sample{};
      if (imuDiagnostics_.receive(sample)) {
        imuReported_ = imuAccepted;
        printImuStatus(sample);
      }
    }
  }

 private:
  void processChar(char ch, NowProvider nowProvider, void* nowContext) noexcept {
    if (ch == '\r' || ch == '\n') {
      commandLine_[commandLength_] = '\0';
      logging::LoggerCommandKind kind{};
      bool recognized = false;
      if (std::strcmp(commandLine_, "START") == 0) {
        kind = logging::LoggerCommandKind::Start;
        recognized = true;
      } else if (std::strcmp(commandLine_, "STOP") == 0) {
        kind = logging::LoggerCommandKind::Stop;
        recognized = true;
      } else if (std::strcmp(commandLine_, "MARKER") == 0) {
        kind = logging::LoggerCommandKind::Marker;
        recognized = true;
      } else if (commandLength_ == 5 && std::strncmp(commandLine_, "TAB ", 4) == 0 &&
                 commandLine_[4] >= '0' && commandLine_[4] <= '3') {
        tabRequest_.store(static_cast<uint8_t>(commandLine_[4] - '0'),
                          std::memory_order_release);
        serial_.printf("SERIAL_CMD TAB %c QUEUED\n", commandLine_[4]);
      } else if (std::strcmp(commandLine_, "STATUS") == 0 ||
                 std::strcmp(commandLine_, "SD_STATUS") == 0) {
        printStorageStatus();
      } else if (commandLength_ != 0) {
        serial_.printf("SERIAL_CMD UNKNOWN=%s\n", commandLine_);
      }
      if (recognized) {
        const uint64_t timestamp = nowProvider != nullptr ? nowProvider(nowContext) : 0;
        const logging::LoggerCommand command{kind, timestamp};
        const bool queued = commands_.trySend(command);
        serial_.printf("SERIAL_CMD %s %s ts=%llu\n", commandLine_,
                       queued ? "QUEUED" : "DROPPED",
                       static_cast<unsigned long long>(command.timestampUs));
      }
      commandLength_ = 0;
    } else if (commandLength_ + 1U < sizeof(commandLine_)) {
      commandLine_[commandLength_++] = ch;
    } else {
      commandLength_ = 0;
      serial_.println("SERIAL_CMD OVERLONG");
    }
  }

  void printStorageStatus() noexcept {
    if (haveLoggerStatus_) {
      serial_.printf("SD_STATUS present=%u logger_state=%u error=%u\n",
                     lastLoggerStatus.storagePresent ? 1U : 0U,
                     static_cast<unsigned>(lastLoggerStatus.state),
                     static_cast<unsigned>(lastLoggerStatus.lastError));
    } else {
      serial_.println("SD_STATUS WAITING_FOR_LOGGER_STATUS");
    }
  }

  void printLoggerStatus(const logging::LoggerStatus& status) noexcept {
    serial_.printf("SPROTZ_LOGGER state=%u error=%u records=%lu events=%lu queue_drops=%lu "
                   "snapshot_queue_drops=%lu imu_queue_drops=%lu command_queue_drops=%lu "
                   "imu_samples=%lu bytes=%llu storage_present=%u file=%s\n",
                   static_cast<unsigned>(status.state),
                   static_cast<unsigned>(status.lastError),
                   static_cast<unsigned long>(status.snapshotsWritten),
                   static_cast<unsigned long>(status.eventsWritten),
                   static_cast<unsigned long>(status.queueDrops),
                   static_cast<unsigned long>(status.snapshotQueueDrops),
                   static_cast<unsigned long>(status.imuQueueDrops),
                   static_cast<unsigned long>(status.commandQueueDrops),
                   static_cast<unsigned long>(status.imuSamplesMerged),
                   static_cast<unsigned long long>(status.bytesWritten),
                   status.storagePresent ? 1U : 0U, status.fileName.data());
  }

  void printSnapshot(const ui::MeasurementSnapshot& snapshot) noexcept {
    serial_.printf("KWP_SNAPSHOT generation=%lu session=%lu seq=%lu rpm=%u coolant_raw=%u "
                   "iat_raw=%u battery_raw=%u g69_raw=%u k409=%u kwp=%u ecu=%u validity=%u "
                   "frames=%lu frame_drops=%lu rx_drops=%lu snapshot_overwrites=%lu "
                   "parser_rejects=%lu byte_fault=%u action_failures=%lu faults=%lu\n",
                   static_cast<unsigned long>(snapshot.transportGeneration),
                   static_cast<unsigned long>(snapshot.sessionEpoch),
                   static_cast<unsigned long>(snapshot.lastRxSequence), snapshot.rpm,
                   snapshot.coolantRaw, snapshot.iatRaw, snapshot.batteryRaw,
                   snapshot.g69Raw, snapshot.k409Connected ? 1U : 0U,
                   snapshot.kwpConnected ? 1U : 0U, snapshot.ecuDataValid ? 1U : 0U,
                   static_cast<unsigned>(snapshot.validity),
                   static_cast<unsigned long>(snapshot.frameCount),
                   static_cast<unsigned long>(snapshot.frameDrops),
                   static_cast<unsigned long>(snapshot.rxIngressDrops),
                   static_cast<unsigned long>(snapshot.snapshotOverwrites),
                   static_cast<unsigned long>(snapshot.parserRejects),
                   static_cast<unsigned>(snapshot.byteFault),
                   static_cast<unsigned long>(snapshot.actionFailures),
                   static_cast<unsigned long>(snapshot.faultCount));
    for (uint8_t i = 0; i < snapshot.fieldCount; ++i) {
      const auto& field = snapshot.fields[i];
      serial_.printf("KWP_FIELD group=%u zone=%u raw=%u formula=0x%02X nwb=%u value=%.3f unit=%u "
                     "semantic=%u semantic_evidence=%u formula_evidence=%u status=%u ts=%llu "
                     "seq=%lu session=%lu generation=%lu\n",
                     static_cast<unsigned>(field.group), static_cast<unsigned>(field.zone),
                     static_cast<unsigned>(field.raw), static_cast<unsigned>(field.formula),
                     static_cast<unsigned>(field.nwb), static_cast<double>(field.decodedValue),
                     static_cast<unsigned>(field.unit), static_cast<unsigned>(field.semantic),
                     static_cast<unsigned>(field.semanticEvidence),
                     static_cast<unsigned>(field.formulaEvidence),
                     static_cast<unsigned>(field.status),
                     static_cast<unsigned long long>(field.timestampUs),
                     static_cast<unsigned long>(field.rxSequence),
                     static_cast<unsigned long>(field.sessionEpoch),
                     static_cast<unsigned long>(field.transportGeneration));
    }
  }

  void printImuStatus(const imu::ImuSample& sample) noexcept {
    serial_.printf("IMU_STATUS samples=%lu seq=%lu ts=%llu ax_mg=%ld ay_mg=%ld az_mg=%ld "
                   "gx_mdps=%ld gy_mdps=%ld gz_mdps=%ld validity=%u\n",
                   static_cast<unsigned long>(imuReported_),
                   static_cast<unsigned long>(sample.sequence),
                   static_cast<unsigned long long>(sample.timestampUs),
                   static_cast<long>(sample.accelXMg), static_cast<long>(sample.accelYMg),
                   static_cast<long>(sample.accelZMg), static_cast<long>(sample.gyroXMdps),
                   static_cast<long>(sample.gyroYMdps), static_cast<long>(sample.gyroZMdps),
                   static_cast<unsigned>(sample.validity));
  }

  logging::LoggerCommandQueue& commands_;
  logging::LoggerStatusMailbox& loggerStatus_;
  ui::LatestSnapshotMailbox& snapshots_;
  imu::ImuDiagnosticsMailbox& imuDiagnostics_;
  std::atomic<uint8_t>& tabRequest_;
  SerialIo& serial_;
  uint32_t imuReported_ = 0;
  char commandLine_[32]{};
  uint8_t commandLength_ = 0;
  logging::LoggerStatus lastLoggerStatus{};
  bool haveLoggerStatus_ = false;
};

}  // namespace digifant::serial
