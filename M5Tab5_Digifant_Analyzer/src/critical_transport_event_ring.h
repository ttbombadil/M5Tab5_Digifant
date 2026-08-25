#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace digifant::transport {

enum class CriticalTransportEventKind : uint8_t { Completion = 1, Disconnect = 2 };

struct CriticalTransportEvent {
  CriticalTransportEventKind kind = CriticalTransportEventKind::Completion;
  uint8_t status = 0;
  uint8_t address = 0;
  uint32_t generation = 0;
  uint64_t operationId = 0;
  uint8_t operationKind = 0;
  uint64_t sequence = 0;
  uint32_t atUs = 0;
};

class CriticalTransportEventRing {
 public:
  static constexpr uint16_t kCapacity = 33;
  static constexpr uint16_t kUsableSlots = kCapacity - 1;

  bool tryPush(const CriticalTransportEvent& event) noexcept {
    const uint16_t head = head_.load(std::memory_order_relaxed);
    const uint16_t next = static_cast<uint16_t>((head + 1U) % kCapacity);
    if (next == tail_.load(std::memory_order_acquire)) {
      overflow_sticky_.store(true, std::memory_order_release);
      drops_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    slots_[head] = event;
    head_.store(next, std::memory_order_release);
    return true;
  }

  bool tryPop(CriticalTransportEvent& event) noexcept {
    const uint16_t tail = tail_.load(std::memory_order_relaxed);
    const uint16_t head = head_.load(std::memory_order_acquire);
    if (tail == head) return false;
    event = slots_[tail];
    tail_.store(static_cast<uint16_t>((tail + 1U) % kCapacity),
                std::memory_order_release);
    return true;
  }

  bool overflowSticky() const noexcept { return overflow_sticky_.load(std::memory_order_acquire); }
  uint32_t drops() const noexcept { return drops_.load(std::memory_order_acquire); }

 private:
  std::array<CriticalTransportEvent, kCapacity> slots_{};
  std::atomic<uint16_t> head_{0};
  std::atomic<uint16_t> tail_{0};
  std::atomic<bool> overflow_sticky_{false};
  std::atomic<uint32_t> drops_{0};
};

}  // namespace digifant::transport
