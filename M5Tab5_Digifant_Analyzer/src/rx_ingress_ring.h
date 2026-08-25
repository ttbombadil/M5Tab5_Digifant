#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace digifant::transport {

struct RxIngressItem {
  uint8_t byte = 0;
  uint32_t batchTimestampUs = 0;
  uint32_t transportGeneration = 0;
  uint32_t ingressEpoch = 0;
  uint64_t transportEventSequence = 0;
};

class RxIngressRing {
 public:
  static constexpr std::size_t kCapacity = 512;
  static constexpr std::size_t kUsableSlots = kCapacity - 1;

  enum class State : uint8_t { Open, Poisoned };

  bool beginProducerBatch() noexcept {
    const bool was_active = producer_active_.exchange(true, std::memory_order_acq_rel);
    if (was_active) return false;
    batch_state_ = state_.load(std::memory_order_acquire);
    batch_epoch_ = epoch_.load(std::memory_order_acquire);
    return true;
  }

  void endProducerBatch() noexcept { producer_active_.store(false, std::memory_order_release); }

  bool publishOne(const uint8_t byte, uint32_t timestamp_us, uint32_t generation,
                  uint64_t sequence) noexcept {
    if (!producer_active_.load(std::memory_order_acquire) || batch_state_ != State::Open) {
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    const uint16_t next = static_cast<uint16_t>((head_ + 1U) % kCapacity);
    if (next == tail_.load(std::memory_order_acquire)) {
      state_.store(State::Poisoned, std::memory_order_release);
      overflow_sticky_.store(true, std::memory_order_release);
      dropped_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    slots_[head_] = RxIngressItem{byte, timestamp_us, generation, batch_epoch_, sequence};
    head_ = next;
    published_count_.fetch_add(1, std::memory_order_relaxed);
    const uint16_t used = static_cast<uint16_t>((head_ + kCapacity -
                                                 tail_.load(std::memory_order_relaxed)) % kCapacity);
    if (used > high_watermark_.load(std::memory_order_relaxed))
      high_watermark_.store(used, std::memory_order_relaxed);
    return true;
  }

  // The callback calls beginProducerBatch(), publishes one complete batch, then ends it.
  // A batch that starts poisoned, or becomes poisoned while publishing, is drop-only.
  std::size_t publishBatch(const uint8_t* bytes, std::size_t count,
                           uint32_t timestamp_us, uint32_t generation,
                           uint64_t first_sequence) noexcept {
    if (bytes == nullptr && count != 0) return 0;
    if (!beginProducerBatch()) return 0;
    std::size_t published = 0;
    if (batch_state_ == State::Open) {
      for (; published < count; ++published)
        if (!publishOne(bytes[published], timestamp_us, generation, first_sequence + published))
          break;
      if (published < count) dropped_.fetch_add(count - published - 1, std::memory_order_relaxed);
    } else {
      dropped_.fetch_add(count, std::memory_order_relaxed);
    }
    endProducerBatch();
    return published;
  }

  bool tryPop(RxIngressItem& out) noexcept {
    const uint16_t tail = tail_.load(std::memory_order_relaxed);
    const uint16_t head = head_; // only producer writes head
    if (tail == head) return false;
    out = slots_[tail];
    tail_.store(static_cast<uint16_t>((tail + 1U) % kCapacity), std::memory_order_release);
    return true;
  }

  bool resetAfterQuiescence() noexcept {
    if (producer_active_.load(std::memory_order_acquire)) return false;
    if (state_.load(std::memory_order_acquire) != State::Poisoned) return false;
    if (producer_active_.load(std::memory_order_acquire)) return false;
    tail_.store(head_, std::memory_order_release);
    epoch_.fetch_add(1, std::memory_order_acq_rel);
    state_.store(State::Open, std::memory_order_release);
    return true;
  }

  bool resetRuntimeMetricsAfterQuiescence() noexcept {
    if (producer_active_.load(std::memory_order_acquire)) return false;
    if (state_.load(std::memory_order_acquire) != State::Open) return false;
    dropped_.store(0, std::memory_order_release);
    published_count_.store(0, std::memory_order_release);
    high_watermark_.store(0, std::memory_order_release);
    return true;
  }

  State state() const noexcept { return state_.load(std::memory_order_acquire); }
  uint32_t epoch() const noexcept { return epoch_.load(std::memory_order_acquire); }
  bool overflowSticky() const noexcept { return overflow_sticky_.load(std::memory_order_acquire); }
  uint32_t dropped() const noexcept { return dropped_.load(std::memory_order_acquire); }
  uint32_t published() const noexcept { return published_count_.load(std::memory_order_acquire); }
  uint16_t highWatermark() const noexcept { return high_watermark_.load(std::memory_order_acquire); }
  bool producerQuiescent() const noexcept { return !producer_active_.load(std::memory_order_acquire); }

 private:
  std::array<RxIngressItem, kCapacity> slots_{};
  uint16_t head_ = 0;
  std::atomic<uint16_t> tail_{0};
  std::atomic<State> state_{State::Open};
  std::atomic<uint32_t> epoch_{1};
  std::atomic<bool> producer_active_{false};
  std::atomic<bool> overflow_sticky_{false};
  std::atomic<uint32_t> dropped_{0};
  std::atomic<uint32_t> published_count_{0};
  std::atomic<uint16_t> high_watermark_{0};
  State batch_state_ = State::Open;
  uint32_t batch_epoch_ = 1;
};

}  // namespace digifant::transport
