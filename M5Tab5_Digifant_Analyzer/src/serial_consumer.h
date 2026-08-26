#pragma once

#include "imu_sample_ring.h"
#include "logger_channels.h"
#include "measurement_snapshot_mailbox.h"
#include "runtime_debug.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>

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
                 SerialIo& serial, runtime::RuntimeDebug* runtimeDebug = nullptr)
      : commands_(commands),
        loggerStatus_(loggerStatus),
        snapshots_(snapshots),
        imuDiagnostics_(imuDiagnostics),
        tabRequest_(tabRequest),
        serial_(serial),
        runtimeDebug_(runtimeDebug) {}

  void poll(NowProvider nowProvider, void* nowContext,
            StackFreeProvider stackFreeProvider = nullptr,
            void* stackFreeContext = nullptr) noexcept {
    const uint64_t nowUs = nowProvider != nullptr ? nowProvider(nowContext) : 0;
    if (runtimeDebug_ != nullptr) {
      runtimeDebug_->noteSerialPoll();
      runtimeDebug_->setPhase(runtime::DebugTask::Serial, runtime::DebugPhase::SerialPoll, nowUs);
    }
    while (serial_.available() > 0) {
      if (runtimeDebug_ != nullptr) runtimeDebug_->noteSerialRxBytes(1);
      processChar(static_cast<char>(serial_.read()), nowProvider, nowContext);
    }

    logging::LoggerStatus loggerStatus{};
    if (loggerStatus_.receive(loggerStatus)) {
      lastLoggerStatus = loggerStatus;
      haveLoggerStatus_ = true;
      setDebugPhase(runtime::DebugPhase::SerialLoggerOutput, nowProvider, nowContext);
      printLoggerStatus(loggerStatus);
      if (stackFreeProvider != nullptr)
        serial_.printf("SPROTZ_STACK_FREE_WORDS=%lu\n",
                       static_cast<unsigned long>(stackFreeProvider(stackFreeContext)));
    }

    ui::MeasurementSnapshot snapshot{};
    if (snapshots_.receive(snapshot)) {
      setDebugPhase(runtime::DebugPhase::SerialSnapshotOutput, nowProvider, nowContext);
      printSnapshot(snapshot);
    }

    const uint32_t imuAccepted = imuDiagnostics_.accepted();
    if (imuAccepted - imuReported_ >= 125U) {
      imu::ImuSample sample{};
      if (imuDiagnostics_.receive(sample)) {
        imuReported_ = imuAccepted;
        setDebugPhase(runtime::DebugPhase::SerialImuOutput, nowProvider, nowContext);
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
      if (std::strcmp(commandLine_, "LOG_START") == 0 ||
                 std::strcmp(commandLine_, "START") == 0) {
        kind = logging::LoggerCommandKind::LogStart;
        recognized = true;
      } else if (std::strcmp(commandLine_, "LOG_STOP") == 0 ||
                 std::strcmp(commandLine_, "STOP") == 0) {
        kind = logging::LoggerCommandKind::LogStop;
        recognized = true;
      } else if (std::strcmp(commandLine_, "SPROTZ_START") == 0) {
        kind = logging::LoggerCommandKind::SprotzStart;
        recognized = true;
      } else if (std::strcmp(commandLine_, "SPROTZ_STOP") == 0) {
        kind = logging::LoggerCommandKind::SprotzStop;
        recognized = true;
      } else if (std::strcmp(commandLine_, "MARKER") == 0) {
        kind = logging::LoggerCommandKind::Marker;
        recognized = true;
      } else if (commandLength_ == 5 && std::strncmp(commandLine_, "TAB ", 4) == 0 &&
                 commandLine_[4] >= '0' && commandLine_[4] <= '3') {
        tabRequest_.store(static_cast<uint8_t>(commandLine_[4] - '0'),
                          std::memory_order_release);
        if (runtimeDebug_ != nullptr) {
          runtimeDebug_->noteSerialCommand();
          (void)runtimeDebug_->noteTabRequest(
              static_cast<uint8_t>(commandLine_[4] - '0'),
              nowProvider != nullptr ? nowProvider(nowContext) : 0);
        }
        setDebugPhase(runtime::DebugPhase::SerialResponseOutput, nowProvider, nowContext);
        responseStarted();
        serial_.printf("SERIAL_CMD TAB %c QUEUED\n", commandLine_[4]);
        responseCompleted();
      } else if (std::strcmp(commandLine_, "STATUS") == 0 ||
                 std::strcmp(commandLine_, "SD_STATUS") == 0) {
        if (runtimeDebug_ != nullptr) runtimeDebug_->noteSerialCommand();
        setDebugPhase(runtime::DebugPhase::SerialResponseOutput, nowProvider, nowContext);
        printStorageStatus();
      } else if (std::strcmp(commandLine_, "DEBUG_STATUS") == 0 && runtimeDebug_ != nullptr) {
        runtimeDebug_->noteSerialCommand();
        setDebugPhase(runtime::DebugPhase::SerialDebugOutput, nowProvider, nowContext);
        printDebugStatus();
      } else if (commandLength_ != 0) {
        setDebugPhase(runtime::DebugPhase::SerialResponseOutput, nowProvider, nowContext);
        responseStarted();
        serial_.printf("SERIAL_CMD UNKNOWN=%s\n", commandLine_);
        responseCompleted();
      }
      if (recognized) {
        if (runtimeDebug_ != nullptr) runtimeDebug_->noteSerialCommand();
        const uint64_t timestamp = nowProvider != nullptr ? nowProvider(nowContext) : 0;
        const logging::LoggerCommand command{kind, timestamp};
        const bool queued = commands_.trySend(command);
        setDebugPhase(runtime::DebugPhase::SerialResponseOutput, nowProvider, nowContext);
        responseStarted();
        serial_.printf("SERIAL_CMD %s %s ts=%llu\n", commandLine_,
                       queued ? "QUEUED" : "DROPPED",
                       static_cast<unsigned long long>(command.timestampUs));
        responseCompleted();
      }
      commandLength_ = 0;
    } else if (commandLength_ + 1U < sizeof(commandLine_)) {
      commandLine_[commandLength_++] = ch;
    } else {
      commandLength_ = 0;
      setDebugPhase(runtime::DebugPhase::SerialResponseOutput, nowProvider, nowContext);
      responseStarted();
      serial_.println("SERIAL_CMD OVERLONG");
      responseCompleted();
    }
  }

  void printStorageStatus() noexcept {
    if (haveLoggerStatus_) {
      responseStarted();
      serial_.printf("SD_STATUS present=%u logger_state=%u error=%u\n",
                     lastLoggerStatus.storagePresent ? 1U : 0U,
                     static_cast<unsigned>(lastLoggerStatus.state),
                     static_cast<unsigned>(lastLoggerStatus.lastError));
      responseCompleted();
    } else {
      responseStarted();
      serial_.println("SD_STATUS WAITING_FOR_LOGGER_STATUS");
      responseCompleted();
    }
  }

  void setDebugPhase(runtime::DebugPhase phase, NowProvider nowProvider,
                     void* nowContext) noexcept {
    if (runtimeDebug_ != nullptr)
      runtimeDebug_->setPhase(runtime::DebugTask::Serial, phase,
                              nowProvider != nullptr ? nowProvider(nowContext) : 0);
  }

  void printLoggerStatus(const logging::LoggerStatus& status) noexcept {
    serial_.printf("SPROTZ_LOGGER state=%u error=%u sprotz_active=%u records=%lu events=%lu queue_drops=%lu "
                   "snapshot_queue_drops=%lu imu_queue_drops=%lu command_queue_drops=%lu "
                   "imu_samples=%lu bytes=%llu storage_present=%u file=%s\n",
                   static_cast<unsigned>(status.state),
                   static_cast<unsigned>(status.lastError),
                   status.sprotzActive ? 1U : 0U,
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

  static const char* debugTaskName(runtime::DebugTask task) noexcept {
    switch (task) {
      case runtime::DebugTask::Display: return "display";
      case runtime::DebugTask::Serial: return "serial";
      case runtime::DebugTask::Processing: return "processing";
      case runtime::DebugTask::Imu: return "imu";
      case runtime::DebugTask::Logger: return "logger";
      case runtime::DebugTask::Arduino: return "arduino";
      default: return "unknown";
    }
  }

  void printDebugStatus() noexcept {
    if (runtimeDebug_ == nullptr) return;
    responseStarted();
    for (uint8_t index = 0; index < static_cast<uint8_t>(runtime::DebugTask::Count); ++index) {
      const auto task = static_cast<runtime::DebugTask>(index);
      const auto& state = runtimeDebug_->task(task);
      uint32_t stack = state.stackHighWaterWords.load(std::memory_order_acquire);
      if (stack == std::numeric_limits<uint32_t>::max()) stack = 0;
      serial_.printf("DEBUG_TASK name=%s handle=0x%lx hb=%lu alive_us=%llu phase=%u max_us=%lu stack_hwm=%lu\n",
                     debugTaskName(task),
                     static_cast<unsigned long>(state.taskHandle.load(std::memory_order_acquire)),
                     static_cast<unsigned long>(state.heartbeat.load(std::memory_order_acquire)),
                     static_cast<unsigned long long>(state.lastAliveUs.load(std::memory_order_acquire)),
                     static_cast<unsigned>(state.phase.load(std::memory_order_acquire)),
                     static_cast<unsigned long>(state.maxLoopUs.load(std::memory_order_acquire)),
                     static_cast<unsigned long>(stack));
    }
    serial_.printf("DEBUG_DISPLAY requested_tab=%u requested_seq=%lu applied_tab=%u applied_seq=%lu rendered_tab=%u rendered_seq=%lu\n",
                   static_cast<unsigned>(runtimeDebug_->requestedTab()),
                   static_cast<unsigned long>(runtimeDebug_->requestedTabSequence()),
                   static_cast<unsigned>(runtimeDebug_->appliedTab()),
                   static_cast<unsigned long>(runtimeDebug_->appliedTabSequence()),
                   static_cast<unsigned>(runtimeDebug_->renderedTab()),
                   static_cast<unsigned long>(runtimeDebug_->renderedTabSequence()));
    serial_.printf("DEBUG_SERIAL polls=%lu rx_bytes=%lu commands=%lu tx_started=%lu tx_completed=%lu\n",
                   static_cast<unsigned long>(runtimeDebug_->serialPolls()),
                   static_cast<unsigned long>(runtimeDebug_->serialRxBytes()),
                   static_cast<unsigned long>(runtimeDebug_->serialCommands()),
                   static_cast<unsigned long>(runtimeDebug_->serialResponsesStarted()),
                   static_cast<unsigned long>(runtimeDebug_->serialResponsesCompleted()));
    serial_.printf("DEBUG_TOUCH samples=%lu presses=%lu count=%u x=%d y=%d last_us=%llu\n",
                   static_cast<unsigned long>(runtimeDebug_->touchSamples()),
                   static_cast<unsigned long>(runtimeDebug_->touchPresses()),
                   static_cast<unsigned>(runtimeDebug_->touchCount()),
                   static_cast<int>(runtimeDebug_->lastTouchX()),
                   static_cast<int>(runtimeDebug_->lastTouchY()),
                   static_cast<unsigned long long>(runtimeDebug_->lastTouchUs()));
    serial_.printf("DEBUG_MEMORY internal_free=%lu internal_largest=%lu psram_free=%lu psram_largest=%lu\n",
                   static_cast<unsigned long>(runtimeDebug_->internalFree()),
                   static_cast<unsigned long>(runtimeDebug_->internalLargest()),
                   static_cast<unsigned long>(runtimeDebug_->psramFree()),
                   static_cast<unsigned long>(runtimeDebug_->psramLargest()));
    const uint32_t last = runtimeDebug_->flightSequence();
    const uint32_t first = last > 16U ? last - 15U : 1U;
    for (uint32_t sequence = first; sequence <= last; ++sequence) {
      const auto entry = runtimeDebug_->flight(sequence);
      if (!entry.valid) continue;
      serial_.printf("DEBUG_FLIGHT seq=%lu ts=%llu task=%s phase=%u value=%lu\n",
                     static_cast<unsigned long>(entry.sequence),
                     static_cast<unsigned long long>(entry.timestampUs), debugTaskName(entry.task),
                     static_cast<unsigned>(entry.phase), static_cast<unsigned long>(entry.value));
    }
    responseCompleted();
  }

  void responseStarted() noexcept {
    if (runtimeDebug_ != nullptr) runtimeDebug_->noteSerialResponseStarted();
  }

  void responseCompleted() noexcept {
    if (runtimeDebug_ != nullptr) runtimeDebug_->noteSerialResponseCompleted();
  }

  logging::LoggerCommandQueue& commands_;
  logging::LoggerStatusMailbox& loggerStatus_;
  ui::LatestSnapshotMailbox& snapshots_;
  imu::ImuDiagnosticsMailbox& imuDiagnostics_;
  std::atomic<uint8_t>& tabRequest_;
  SerialIo& serial_;
  runtime::RuntimeDebug* runtimeDebug_ = nullptr;
  uint32_t imuReported_ = 0;
  char commandLine_[32]{};
  uint8_t commandLength_ = 0;
  logging::LoggerStatus lastLoggerStatus{};
  bool haveLoggerStatus_ = false;
};

}  // namespace digifant::serial
