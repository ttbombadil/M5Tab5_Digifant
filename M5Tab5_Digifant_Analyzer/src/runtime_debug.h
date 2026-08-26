#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

#ifndef TAB5_RUNTIME_DEBUG
#define TAB5_RUNTIME_DEBUG 0
#endif

// This state exists only for the explicit TAB5_RUNTIME_DEBUG target build.
// It is fixed-size, lock-free and never emits output from producer tasks.
namespace digifant::runtime {

enum class DebugTask : uint8_t { Display, Serial, Processing, Imu, Logger, Arduino, Count };

enum class DebugPhase : uint8_t {
  Idle,
  LoopBegin,
  ProcessingPoll,
  SerialPoll,
  SerialLoggerOutput,
  SerialSnapshotOutput,
  SerialImuOutput,
  SerialResponseOutput,
  SerialDebugOutput,
  DisplayBeforeM5Update,
  DisplayAfterM5Update,
  DisplayBeforeTouch,
  DisplayAfterTouch,
  DisplayBeforeDraw,
  DisplayAfterDraw,
  ImuPoll,
  LoggerPoll,
  ArduinoBeforeSession,
  ArduinoAfterSession,
};

struct DebugTaskState {
  std::atomic<uintptr_t> taskHandle{0};
  std::atomic<uint32_t> heartbeat{0};
  std::atomic<uint64_t> lastAliveUs{0};
  std::atomic<uint8_t> phase{static_cast<uint8_t>(DebugPhase::Idle)};
  std::atomic<uint32_t> maxLoopUs{0};
  std::atomic<uint32_t> stackHighWaterWords{std::numeric_limits<uint32_t>::max()};
};

struct FlightRecord {
  std::atomic<uint32_t> sequence{0};
  std::atomic<uint64_t> timestampUs{0};
  std::atomic<uint32_t> metadata{0};
  std::atomic<uint32_t> value{0};
};

struct FlightRecordView {
  uint32_t sequence = 0;
  uint64_t timestampUs = 0;
  DebugTask task = DebugTask::Arduino;
  DebugPhase phase = DebugPhase::Idle;
  uint32_t value = 0;
  bool valid = false;
};

class RuntimeDebug final {
 public:
  static constexpr std::size_t kTaskCount = static_cast<std::size_t>(DebugTask::Count);
  static constexpr std::size_t kFlightCapacity = 256;

  void beginLoop(DebugTask task, DebugPhase phase, uint64_t nowUs,
                 uint32_t stackHighWaterWords) noexcept {
    auto& state = tasks_[index(task)];
    state.heartbeat.fetch_add(1, std::memory_order_relaxed);
    state.lastAliveUs.store(nowUs, std::memory_order_release);
    state.phase.store(static_cast<uint8_t>(phase), std::memory_order_release);
    updateMinimum(state.stackHighWaterWords, stackHighWaterWords);
  }

  void noteTaskHandle(DebugTask task, uintptr_t handle) noexcept {
    tasks_[index(task)].taskHandle.store(handle, std::memory_order_release);
  }

  void setPhase(DebugTask task, DebugPhase phase, uint64_t nowUs) noexcept {
    auto& state = tasks_[index(task)];
    state.lastAliveUs.store(nowUs, std::memory_order_release);
    state.phase.store(static_cast<uint8_t>(phase), std::memory_order_release);
    if (task == DebugTask::Display && phase >= DebugPhase::DisplayBeforeM5Update &&
        phase <= DebugPhase::DisplayAfterDraw)
      record(nowUs, task, phase, 0);
  }

  void observeLoopDuration(DebugTask task, uint32_t elapsedUs) noexcept {
    updateMaximum(tasks_[index(task)].maxLoopUs, elapsedUs);
  }

  const DebugTaskState& task(DebugTask id) const noexcept { return tasks_[index(id)]; }

  uint32_t noteTabRequest(uint8_t tab, uint64_t nowUs,
                          DebugTask sourceTask = DebugTask::Serial,
                          DebugPhase sourcePhase = DebugPhase::SerialPoll) noexcept {
    const uint32_t sequence = tabSequence_.fetch_add(1, std::memory_order_relaxed) + 1U;
    requestedTab_.store(tab, std::memory_order_release);
    requestedTabSequence_.store(sequence, std::memory_order_release);
    record(nowUs, sourceTask, sourcePhase,
           (sequence << 8U) | static_cast<uint32_t>(tab));
    return sequence;
  }

  void noteTabApplied(uint8_t tab, uint32_t sequence, uint64_t nowUs) noexcept {
    appliedTab_.store(tab, std::memory_order_release);
    appliedTabSequence_.store(sequence, std::memory_order_release);
    record(nowUs, DebugTask::Display, DebugPhase::DisplayAfterTouch,
           (sequence << 8U) | static_cast<uint32_t>(tab));
  }

  void noteTabRendered(uint8_t tab, uint32_t sequence, uint64_t nowUs) noexcept {
    renderedTab_.store(tab, std::memory_order_release);
    renderedTabSequence_.store(sequence, std::memory_order_release);
    record(nowUs, DebugTask::Display, DebugPhase::DisplayAfterDraw,
           (sequence << 8U) | static_cast<uint32_t>(tab));
  }

  uint32_t requestedTabSequence() const noexcept {
    return requestedTabSequence_.load(std::memory_order_acquire);
  }
  uint8_t requestedTab() const noexcept { return requestedTab_.load(std::memory_order_acquire); }
  uint32_t appliedTabSequence() const noexcept {
    return appliedTabSequence_.load(std::memory_order_acquire);
  }
  uint8_t appliedTab() const noexcept { return appliedTab_.load(std::memory_order_acquire); }
  uint32_t renderedTabSequence() const noexcept {
    return renderedTabSequence_.load(std::memory_order_acquire);
  }
  uint8_t renderedTab() const noexcept { return renderedTab_.load(std::memory_order_acquire); }

  void noteSerialPoll() noexcept { serialPolls_.fetch_add(1, std::memory_order_relaxed); }
  void noteSerialRxBytes(uint32_t count) noexcept {
    serialRxBytes_.fetch_add(count, std::memory_order_relaxed);
  }
  void noteSerialCommand() noexcept { serialCommands_.fetch_add(1, std::memory_order_relaxed); }
  void noteSerialResponseStarted() noexcept {
    serialResponsesStarted_.fetch_add(1, std::memory_order_relaxed);
  }
  void noteSerialResponseCompleted() noexcept {
    serialResponsesCompleted_.fetch_add(1, std::memory_order_relaxed);
  }
  uint32_t serialPolls() const noexcept { return serialPolls_.load(std::memory_order_acquire); }
  uint32_t serialRxBytes() const noexcept { return serialRxBytes_.load(std::memory_order_acquire); }
  uint32_t serialCommands() const noexcept { return serialCommands_.load(std::memory_order_acquire); }
  uint32_t serialResponsesStarted() const noexcept {
    return serialResponsesStarted_.load(std::memory_order_acquire);
  }
  uint32_t serialResponsesCompleted() const noexcept {
    return serialResponsesCompleted_.load(std::memory_order_acquire);
  }

  void noteTouch(uint8_t count, bool pressed, int16_t x, int16_t y, uint64_t nowUs) noexcept {
    touchSamples_.fetch_add(1, std::memory_order_relaxed);
    touchCount_.store(count, std::memory_order_release);
    if (!pressed) return;
    touchPresses_.fetch_add(1, std::memory_order_relaxed);
    lastTouchX_.store(x, std::memory_order_release);
    lastTouchY_.store(y, std::memory_order_release);
    lastTouchUs_.store(nowUs, std::memory_order_release);
    record(nowUs, DebugTask::Display, DebugPhase::DisplayAfterTouch,
           (static_cast<uint32_t>(static_cast<uint16_t>(x)) << 16U) |
               static_cast<uint16_t>(y));
  }
  uint32_t touchSamples() const noexcept { return touchSamples_.load(std::memory_order_acquire); }
  uint32_t touchPresses() const noexcept { return touchPresses_.load(std::memory_order_acquire); }
  uint8_t touchCount() const noexcept { return touchCount_.load(std::memory_order_acquire); }
  int16_t lastTouchX() const noexcept { return lastTouchX_.load(std::memory_order_acquire); }
  int16_t lastTouchY() const noexcept { return lastTouchY_.load(std::memory_order_acquire); }
  uint64_t lastTouchUs() const noexcept { return lastTouchUs_.load(std::memory_order_acquire); }

  void noteMemory(uint32_t internalFree, uint32_t internalLargest, uint32_t psramFree,
                  uint32_t psramLargest) noexcept {
    internalFree_.store(internalFree, std::memory_order_release);
    internalLargest_.store(internalLargest, std::memory_order_release);
    psramFree_.store(psramFree, std::memory_order_release);
    psramLargest_.store(psramLargest, std::memory_order_release);
  }
  uint32_t internalFree() const noexcept { return internalFree_.load(std::memory_order_acquire); }
  uint32_t internalLargest() const noexcept { return internalLargest_.load(std::memory_order_acquire); }
  uint32_t psramFree() const noexcept { return psramFree_.load(std::memory_order_acquire); }
  uint32_t psramLargest() const noexcept { return psramLargest_.load(std::memory_order_acquire); }

  void record(uint64_t nowUs, DebugTask task, DebugPhase phase, uint32_t value) noexcept {
    const uint32_t sequence = flightSequence_.fetch_add(1, std::memory_order_relaxed) + 1U;
    FlightRecord& slot = flight_[sequence % kFlightCapacity];
    slot.timestampUs.store(nowUs, std::memory_order_relaxed);
    slot.metadata.store((static_cast<uint32_t>(task) << 8U) |
                            static_cast<uint32_t>(phase),
                        std::memory_order_relaxed);
    slot.value.store(value, std::memory_order_relaxed);
    slot.sequence.store(sequence, std::memory_order_release);
  }

  uint32_t flightSequence() const noexcept { return flightSequence_.load(std::memory_order_acquire); }

  FlightRecordView flight(uint32_t sequence) const noexcept {
    const FlightRecord& slot = flight_[sequence % kFlightCapacity];
    if (slot.sequence.load(std::memory_order_acquire) != sequence) return {};
    FlightRecordView result{};
    result.timestampUs = slot.timestampUs.load(std::memory_order_relaxed);
    const uint32_t metadata = slot.metadata.load(std::memory_order_relaxed);
    result.value = slot.value.load(std::memory_order_relaxed);
    if (slot.sequence.load(std::memory_order_acquire) != sequence) return {};
    result.sequence = sequence;
    result.task = static_cast<DebugTask>((metadata >> 8U) & 0xFFU);
    result.phase = static_cast<DebugPhase>(metadata & 0xFFU);
    result.valid = true;
    return result;
  }

 private:
  static constexpr std::size_t index(DebugTask task) noexcept {
    return static_cast<std::size_t>(task);
  }

  static void updateMaximum(std::atomic<uint32_t>& value, uint32_t candidate) noexcept {
    uint32_t observed = value.load(std::memory_order_relaxed);
    while (candidate > observed &&
           !value.compare_exchange_weak(observed, candidate, std::memory_order_relaxed)) {}
  }

  static void updateMinimum(std::atomic<uint32_t>& value, uint32_t candidate) noexcept {
    uint32_t observed = value.load(std::memory_order_relaxed);
    while (candidate < observed &&
           !value.compare_exchange_weak(observed, candidate, std::memory_order_relaxed)) {}
  }

  std::array<DebugTaskState, kTaskCount> tasks_{};
  std::array<FlightRecord, kFlightCapacity> flight_{};
  std::atomic<uint32_t> flightSequence_{0};
  std::atomic<uint32_t> tabSequence_{0};
  std::atomic<uint8_t> requestedTab_{255};
  std::atomic<uint32_t> requestedTabSequence_{0};
  std::atomic<uint8_t> appliedTab_{255};
  std::atomic<uint32_t> appliedTabSequence_{0};
  std::atomic<uint8_t> renderedTab_{255};
  std::atomic<uint32_t> renderedTabSequence_{0};
  std::atomic<uint32_t> serialPolls_{0};
  std::atomic<uint32_t> serialRxBytes_{0};
  std::atomic<uint32_t> serialCommands_{0};
  std::atomic<uint32_t> serialResponsesStarted_{0};
  std::atomic<uint32_t> serialResponsesCompleted_{0};
  std::atomic<uint32_t> touchSamples_{0};
  std::atomic<uint32_t> touchPresses_{0};
  std::atomic<uint8_t> touchCount_{0};
  std::atomic<int16_t> lastTouchX_{0};
  std::atomic<int16_t> lastTouchY_{0};
  std::atomic<uint64_t> lastTouchUs_{0};
  std::atomic<uint32_t> internalFree_{0};
  std::atomic<uint32_t> internalLargest_{0};
  std::atomic<uint32_t> psramFree_{0};
  std::atomic<uint32_t> psramLargest_{0};
};

}  // namespace digifant::runtime
