#pragma once

#include "imu_sampler.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace digifant::imu {

class ImuSampleRing {
 public:
  static constexpr std::size_t kSlotCount = 257;
  static constexpr std::size_t kCapacity = 256;

  bool tryPush(const ImuSample& sample) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = increment(head);
    if (next == tail_.load(std::memory_order_acquire)) {
      drops_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    slots_[head] = sample;
    head_.store(next, std::memory_order_release);
    const uint32_t depth = static_cast<uint32_t>(distance(next, tail_.load(std::memory_order_acquire)));
    uint32_t high = highWatermark_.load(std::memory_order_relaxed);
    while (depth > high && !highWatermark_.compare_exchange_weak(
                               high, depth, std::memory_order_relaxed, std::memory_order_relaxed)) {}
    return true;
  }

  bool tryPop(ImuSample& sample) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    sample = slots_[tail];
    tail_.store(increment(tail), std::memory_order_release);
    return true;
  }

  uint32_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }
  uint32_t highWatermark() const noexcept { return highWatermark_.load(std::memory_order_relaxed); }

 private:
  static constexpr std::size_t increment(std::size_t value) noexcept {
    return value + 1U == kSlotCount ? 0U : value + 1U;
  }
  static constexpr std::size_t distance(std::size_t head, std::size_t tail) noexcept {
    return head >= tail ? head - tail : kSlotCount - tail + head;
  }

  std::array<ImuSample, kSlotCount> slots_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
  std::atomic<uint32_t> drops_{0};
  std::atomic<uint32_t> highWatermark_{0};
};

// The sampler publishes one latest diagnostic copy for the low-rate Serial
// report. It is deliberately separate from ImuSampleRing: the logger remains
// the sole consumer of the bounded sample stream, while this mailbox carries
// only an observational latest value.
class ImuDiagnosticsMailbox {
 public:
  void publish(const ImuSample& sample) noexcept {
    accepted_.fetch_add(1, std::memory_order_relaxed);
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
    if (write == kNoSlot) {
      publishDrops_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    const uint32_t publication = published_.load(std::memory_order_relaxed) + 1U;
    slots_[write].value = sample;
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

  bool receive(ImuSample& sample) noexcept {
    const uint32_t published = published_.load(std::memory_order_acquire);
    if (published == consumed_) return false;
    for (uint8_t attempt = 0; attempt < kSlotCount; ++attempt) {
      const uint8_t read = publishedIndex_.load(std::memory_order_acquire);
      if (read == kNoSlot) return false;
      SlotState expected = SlotState::Published;
      if (!slots_[read].state.compare_exchange_strong(expected, SlotState::Reading,
                                                       std::memory_order_acquire,
                                                       std::memory_order_relaxed)) continue;
      sample = slots_[read].value;
      consumed_ = slots_[read].publication;
      slots_[read].state.store(SlotState::Free, std::memory_order_release);
      return true;
    }
    return false;
  }

  uint32_t accepted() const noexcept { return accepted_.load(std::memory_order_acquire); }
  uint32_t publishDrops() const noexcept {
    return publishDrops_.load(std::memory_order_relaxed);
  }

 private:
  enum class SlotState : uint8_t { Free, Writing, Published, Reading };
  struct Slot {
    ImuSample value{};
    uint32_t publication = 0;
    std::atomic<SlotState> state{SlotState::Free};
  };
  static constexpr uint8_t kSlotCount = 3;
  static constexpr uint8_t kNoSlot = 0xFF;
  std::array<Slot, kSlotCount> slots_{};
  std::atomic<uint8_t> publishedIndex_{kNoSlot};
  std::atomic<uint32_t> published_{0};
  std::atomic<uint32_t> accepted_{0};
  std::atomic<uint32_t> publishDrops_{0};
  uint32_t consumed_ = 0;
};

}  // namespace digifant::imu
