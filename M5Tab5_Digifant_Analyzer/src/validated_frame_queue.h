#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace digifant::transport {

struct KwpFrameEnvelope {
  uint8_t size = 0;
  std::array<uint8_t, 65> bytes{};
  uint8_t counter = 0;
  uint8_t title = 0;
  uint32_t rxSequence = 0;
  uint32_t sessionEpoch = 0;
  uint64_t firstByteUs = 0;
  uint64_t completedUs = 0;
  uint32_t requestId = 0;
  uint8_t dialogContext = 0;
  uint8_t groupHint = 0;
  uint32_t transportGeneration = 0;
  uint32_t ingressEpoch = 0;
};

template <std::size_t Capacity>
class DropNewestQueue {
  static_assert(Capacity > 1);
 public:
  bool trySend(KwpFrameEnvelope frame) noexcept {
    frame.rxSequence = nextSequence_.fetch_add(1, std::memory_order_relaxed);
    const uint16_t next = static_cast<uint16_t>((head_ + 1U) % Capacity);
    if (next == tail_.load(std::memory_order_acquire)) {
      drops_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    slots_[head_] = frame;
    head_ = next;
    const uint16_t used = static_cast<uint16_t>((head_ + Capacity - tail_.load(std::memory_order_relaxed)) % Capacity);
    if (used > highWatermark_.load(std::memory_order_relaxed))
      highWatermark_.store(used, std::memory_order_relaxed);
    return true;
  }

  bool tryReceive(KwpFrameEnvelope& frame) noexcept {
    const uint16_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_) return false;
    frame = slots_[tail];
    tail_.store(static_cast<uint16_t>((tail + 1U) % Capacity), std::memory_order_release);
    return true;
  }
  uint32_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }
  uint16_t highWatermark() const noexcept { return highWatermark_.load(std::memory_order_relaxed); }
  uint32_t nextSequence() const noexcept { return nextSequence_.load(std::memory_order_acquire); }

 private:
  std::array<KwpFrameEnvelope, Capacity> slots_{};
  uint16_t head_ = 0;
  std::atomic<uint16_t> tail_{0};
  std::atomic<uint32_t> nextSequence_{0};
  std::atomic<uint32_t> drops_{0};
  std::atomic<uint16_t> highWatermark_{0};
};

using ValidatedFrameQueue = DropNewestQueue<33>;  // 32 usable slots, one sentinel.

}  // namespace digifant::transport
