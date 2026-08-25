#pragma once

#include "logger_types.h"
#include "measurement_snapshot_types.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace digifant::logging {

template <typename T, std::size_t SlotCount>
class FixedSpscQueue {
 public:
  static_assert(SlotCount >= 2);
  static constexpr std::size_t capacity() noexcept { return SlotCount - 1U; }

  bool trySend(const T& value) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = increment(head);
    if (next == tail_.load(std::memory_order_acquire)) {
      drops_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    slots_[head] = value;
    head_.store(next, std::memory_order_release);
    const uint32_t depth = static_cast<uint32_t>(distance(next, tail_.load(std::memory_order_acquire)));
    uint32_t high = highWatermark_.load(std::memory_order_relaxed);
    while (depth > high && !highWatermark_.compare_exchange_weak(
                               high, depth, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    return true;
  }

  bool tryReceive(T& value) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    value = slots_[tail];
    tail_.store(increment(tail), std::memory_order_release);
    return true;
  }

  uint32_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }
  uint32_t highWatermark() const noexcept { return highWatermark_.load(std::memory_order_relaxed); }

 private:
  static constexpr std::size_t increment(std::size_t value) noexcept {
    return value + 1U == SlotCount ? 0U : value + 1U;
  }
  static constexpr std::size_t distance(std::size_t head, std::size_t tail) noexcept {
    return head >= tail ? head - tail : SlotCount - tail + head;
  }

  std::array<T, SlotCount> slots_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
  std::atomic<uint32_t> drops_{0};
  std::atomic<uint32_t> highWatermark_{0};
};

// Logger commands have two independent producers on the target (Serial and
// Display) and one logger-task consumer. The short try-lock makes the
// multi-producer boundary explicit without waiting or allocating. A producer
// that collides with another producer, or encounters a full queue, gets the
// same bounded drop result as a full SPSC queue.
template <typename T, std::size_t SlotCount>
class FixedMpscQueue {
 public:
  static_assert(SlotCount >= 2);
  static constexpr std::size_t capacity() noexcept { return SlotCount - 1U; }

  bool trySend(const T& value) noexcept {
    if (!tryLock()) {
      drops_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    const std::size_t next = increment(head_);
    if (next == tail_) {
      unlock();
      drops_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    slots_[head_] = value;
    head_ = next;
    const uint32_t depth = static_cast<uint32_t>(distance(head_, tail_));
    uint32_t high = highWatermark_.load(std::memory_order_relaxed);
    while (depth > high && !highWatermark_.compare_exchange_weak(
                               high, depth, std::memory_order_relaxed,
                               std::memory_order_relaxed)) {}
    unlock();
    return true;
  }

  bool tryReceive(T& value) noexcept {
    if (!tryLock()) return false;
    if (tail_ == head_) {
      unlock();
      return false;
    }
    value = slots_[tail_];
    tail_ = increment(tail_);
    unlock();
    return true;
  }

  uint32_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }
  uint32_t highWatermark() const noexcept {
    return highWatermark_.load(std::memory_order_relaxed);
  }

 private:
  static constexpr std::size_t increment(std::size_t value) noexcept {
    return value + 1U == SlotCount ? 0U : value + 1U;
  }
  static constexpr std::size_t distance(std::size_t head, std::size_t tail) noexcept {
    return head >= tail ? head - tail : SlotCount - tail + head;
  }
  bool tryLock() noexcept {
    return !operationLock_.test_and_set(std::memory_order_acquire);
  }
  void unlock() noexcept { operationLock_.clear(std::memory_order_release); }

  std::array<T, SlotCount> slots_{};
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  std::atomic_flag operationLock_{};
  std::atomic<uint32_t> drops_{0};
  std::atomic<uint32_t> highWatermark_{0};
};

using LoggerSnapshotQueue = FixedSpscQueue<ui::MeasurementSnapshot, 33>;
using LoggerCommandQueue = FixedMpscQueue<LoggerCommand, 9>;

class LoggerStatusMailbox {
 public:
  LoggerStatusMailbox() noexcept {
    for (auto& slot : slots_) slot.state.store(SlotState::Free, std::memory_order_relaxed);
  }

  void publish(const LoggerStatus& status) noexcept {
    uint8_t write = kNoSlot;
    for (uint8_t i = 0; i < kSlotCount; ++i) {
      SlotState expected = SlotState::Free;
      if (slots_[i].state.compare_exchange_strong(expected, SlotState::Writing,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed)) {
        write = i;
        break;
      }
    }
    if (write == kNoSlot) return;
    const uint32_t publication = published_.load(std::memory_order_relaxed) + 1U;
    slots_[write].value = status;
    slots_[write].publication = publication;
    slots_[write].state.store(SlotState::Published, std::memory_order_release);
    const uint8_t old = publishedIndex_.exchange(write, std::memory_order_acq_rel);
    published_.store(publication, std::memory_order_release);
    if (old != kNoSlot && old != write) {
      SlotState expected = SlotState::Published;
      (void)slots_[old].state.compare_exchange_strong(expected, SlotState::Free,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_relaxed);
    }
  }

  bool receive(LoggerStatus& status) noexcept {
    const uint32_t published = published_.load(std::memory_order_acquire);
    if (published == consumed_) return false;
    for (uint8_t attempt = 0; attempt < kSlotCount; ++attempt) {
      const uint8_t read = publishedIndex_.load(std::memory_order_acquire);
      if (read == kNoSlot) return false;
      SlotState expected = SlotState::Published;
      if (!slots_[read].state.compare_exchange_strong(expected, SlotState::Reading,
                                                       std::memory_order_acquire,
                                                       std::memory_order_relaxed)) continue;
      status = slots_[read].value;
      consumed_ = slots_[read].publication;
      slots_[read].state.store(SlotState::Free, std::memory_order_release);
      return true;
    }
    return false;
  }

 private:
  enum class SlotState : uint8_t { Free, Writing, Published, Reading };
  struct Slot {
    LoggerStatus value{};
    uint32_t publication = 0;
    std::atomic<SlotState> state{SlotState::Free};
  };
  static constexpr uint8_t kSlotCount = 3;
  static constexpr uint8_t kNoSlot = 0xFF;
  std::array<Slot, kSlotCount> slots_{};
  std::atomic<uint8_t> publishedIndex_{kNoSlot};
  std::atomic<uint32_t> published_{0};
  uint32_t consumed_ = 0;
};

class LoggerStatusFanout {
 public:
  void publish(const LoggerStatus& status) noexcept {
    display_.publish(status);
    serial_.publish(status);
  }
  LoggerStatusMailbox& display() noexcept { return display_; }
  LoggerStatusMailbox& serial() noexcept { return serial_; }

 private:
  LoggerStatusMailbox display_{};
  LoggerStatusMailbox serial_{};
};


}  // namespace digifant::logging
