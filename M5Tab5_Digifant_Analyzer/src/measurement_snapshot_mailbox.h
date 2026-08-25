#pragma once

#include "measurement_snapshot_types.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace digifant::ui {

class LatestSnapshotMailbox {
 public:
  LatestSnapshotMailbox() noexcept {
    for (auto& slot : slots_) slot.state.store(SlotState::Free, std::memory_order_relaxed);
  }

  void publish(const MeasurementSnapshot& snapshot) noexcept {
    // Three slots are sufficient for one SPSC reader: at most one slot is
    // being read and one is the currently published value, leaving one slot
    // available to the nonblocking overwrite producer.
    uint8_t write_index = kNoSlot;
    for (uint8_t i = 0; i < kSlotCount; ++i) {
      SlotState expected = SlotState::Free;
      if (slots_[i].state.compare_exchange_strong(expected, SlotState::Writing,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed)) {
        write_index = i;
        break;
      }
    }
    if (write_index == kNoSlot) {
      publishDrops_.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    const uint32_t publication = published_.load(std::memory_order_relaxed) + 1U;
    slots_[write_index].value = snapshot;
    slots_[write_index].publication = publication;
    slots_[write_index].state.store(SlotState::Published, std::memory_order_release);
    const uint8_t old_index = publishedIndex_.exchange(write_index, std::memory_order_acq_rel);
    published_.store(publication, std::memory_order_release);

    if (old_index != kNoSlot && old_index != write_index) {
      SlotState expected = SlotState::Published;
      (void)slots_[old_index].state.compare_exchange_strong(expected, SlotState::Free,
                                                             std::memory_order_acq_rel,
                                                             std::memory_order_relaxed);
    }
  }

  bool receive(MeasurementSnapshot& snapshot) noexcept {
    const uint32_t published = published_.load(std::memory_order_acquire);
    if (published == consumed_) return false;

    for (uint8_t attempt = 0; attempt < kSlotCount; ++attempt) {
      const uint8_t read_index = publishedIndex_.load(std::memory_order_acquire);
      if (read_index == kNoSlot) return false;
      SlotState expected = SlotState::Published;
      if (!slots_[read_index].state.compare_exchange_strong(expected, SlotState::Reading,
                                                             std::memory_order_acquire,
                                                             std::memory_order_relaxed))
        continue;

      snapshot = slots_[read_index].value;
      const uint32_t consumed_publication = slots_[read_index].publication;
      slots_[read_index].state.store(SlotState::Free, std::memory_order_release);
      const uint32_t previous = consumed_;
      consumed_ = consumed_publication;
      if (consumed_publication > previous + 1U)
        overwrites_.fetch_add(1, std::memory_order_relaxed);
      return true;
    }
    return false;
  }

  uint32_t overwrites() const noexcept { return overwrites_.load(std::memory_order_relaxed); }
  uint32_t publishedCount() const noexcept { return published_.load(std::memory_order_acquire); }
  uint32_t publishDrops() const noexcept { return publishDrops_.load(std::memory_order_relaxed); }

 private:
  enum class SlotState : uint8_t { Free, Writing, Published, Reading };
  struct Slot {
    MeasurementSnapshot value{};
    uint32_t publication = 0;
    std::atomic<SlotState> state{SlotState::Free};
  };
  static constexpr uint8_t kSlotCount = 3;
  static constexpr uint8_t kNoSlot = 0xFF;
  std::array<Slot, kSlotCount> slots_{};
  std::atomic<uint8_t> publishedIndex_{kNoSlot};
  std::atomic<uint32_t> published_{0};
  std::atomic<uint32_t> publishDrops_{0};
  uint32_t consumed_ = 0;
  std::atomic<uint32_t> overwrites_{0};
};

class SnapshotConsumerFanout {
 public:
  void publish(const MeasurementSnapshot& snapshot) noexcept {
    serial_.publish(snapshot);
    display_.publish(snapshot);
    bluetooth_.publish(snapshot);
    web_.publish(snapshot);
  }

  LatestSnapshotMailbox& serial() noexcept { return serial_; }
  LatestSnapshotMailbox& display() noexcept { return display_; }
  LatestSnapshotMailbox& bluetooth() noexcept { return bluetooth_; }
  LatestSnapshotMailbox& web() noexcept { return web_; }

 private:
  LatestSnapshotMailbox serial_{};
  LatestSnapshotMailbox display_{};
  LatestSnapshotMailbox bluetooth_{};
  LatestSnapshotMailbox web_{};
};

}  // namespace digifant::ui
