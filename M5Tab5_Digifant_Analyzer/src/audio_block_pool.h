#pragma once

#include "audio_types.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace digifant::audio {

namespace detail {

template <std::size_t Capacity>
class AudioIndexQueue {
 public:
  static_assert(Capacity > 0);

  bool tryPush(uint16_t value) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = increment(head);
    if (next == tail_.load(std::memory_order_acquire)) return false;
    slots_[head] = value;
    head_.store(next, std::memory_order_release);
    return true;
  }

  bool tryPop(uint16_t& value) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    value = slots_[tail];
    tail_.store(increment(tail), std::memory_order_release);
    return true;
  }

  std::size_t depth() const noexcept {
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    return head >= tail ? head - tail : kSlotCount - tail + head;
  }

 private:
  static constexpr std::size_t kSlotCount = Capacity + 1U;

  static constexpr std::size_t increment(std::size_t value) noexcept {
    return value + 1U == kSlotCount ? 0U : value + 1U;
  }

  std::array<uint16_t, kSlotCount> slots_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

inline void saturatingIncrement(std::atomic<uint64_t>& value) noexcept {
  uint64_t current = value.load(std::memory_order_relaxed);
  while (current != std::numeric_limits<uint64_t>::max() &&
         !value.compare_exchange_weak(current, current + 1U, std::memory_order_relaxed,
                                      std::memory_order_relaxed)) {}
}

}  // namespace detail

class AudioBlockPool {
 public:
  AudioBlockPool() noexcept = default;
  AudioBlockPool(const AudioBlockPool&) = delete;
  AudioBlockPool& operator=(const AudioBlockPool&) = delete;

  bool initialize(uint8_t* storage, std::size_t storageBytes) noexcept {
    if (initialized_.load(std::memory_order_acquire) || storage == nullptr ||
        storageBytes < kAudioStorageBytes) {
      return false;
    }

    storage_ = storage;
    for (uint16_t i = 0; i < kAudioBlockCount; ++i) {
      blocks_[i].data = storage_ + static_cast<std::size_t>(i) * kPcmBytesPerBlock;
      blocks_[i].metadata = {};
      blocks_[i].lease.store(0, std::memory_order_relaxed);
      blocks_[i].owner.store(AudioBlockOwner::Free, std::memory_order_relaxed);
      if (!free_.tryPush(i)) return false;
    }
    initialized_.store(true, std::memory_order_release);
    return true;
  }

  bool initialized() const noexcept { return initialized_.load(std::memory_order_acquire); }

  bool tryAcquireForMic(AudioBlockHandle& handle) noexcept {
    handle = {};
    if (!initialized()) return false;
    uint16_t index = kInvalidBlockIndex;
    if (!free_.tryPop(index)) return false;
    if (index >= kAudioBlockCount) {
      noteOwnershipError();
      return false;
    }
    uint64_t lease = blocks_[index].lease.load(std::memory_order_relaxed);
    lease = lease == std::numeric_limits<uint64_t>::max() ? 1U : lease + 1U;
    blocks_[index].lease.store(lease, std::memory_order_release);
    AudioBlockOwner expected = AudioBlockOwner::Free;
    if (!blocks_[index].owner.compare_exchange_strong(
            expected, AudioBlockOwner::MicOwned, std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
      noteOwnershipError();
      return false;
    }
    handle = AudioBlockHandle{index, lease};
    return true;
  }

  uint8_t* writableData(const AudioBlockHandle& handle) noexcept {
    return owns(handle, AudioBlockOwner::MicOwned) ? blocks_[handle.index].data : nullptr;
  }

  bool publishFromMic(const AudioBlockHandle& handle,
                      const AudioBlockMetadata& metadata) noexcept {
    if (!owns(handle, AudioBlockOwner::MicOwned)) {
      noteOwnershipError();
      return false;
    }
    if (!metadataFits(metadata)) return false;
    if (ready_.depth() >= kMaxReadyBlocks) return false;
    blocks_[handle.index].metadata = metadata;
    blocks_[handle.index].owner.store(AudioBlockOwner::Ready, std::memory_order_release);
    if (!ready_.tryPush(handle.index)) {
      blocks_[handle.index].owner.store(AudioBlockOwner::MicOwned, std::memory_order_release);
      return false;
    }
    updateReadyHighWatermark();
    return true;
  }

  bool discardFromMic(const AudioBlockHandle& handle) noexcept {
    return returnToFree(handle, AudioBlockOwner::MicOwned);
  }

  bool tryAcquireForWriter(AudioBlockHandle& handle) noexcept {
    handle = {};
    if (!initialized()) return false;
    uint16_t index = kInvalidBlockIndex;
    if (!ready_.tryPop(index)) return false;
    if (index >= kAudioBlockCount) {
      noteOwnershipError();
      return false;
    }
    AudioBlockOwner expected = AudioBlockOwner::Ready;
    if (!blocks_[index].owner.compare_exchange_strong(
            expected, AudioBlockOwner::WriterOwned, std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
      noteOwnershipError();
      return false;
    }
    handle = AudioBlockHandle{index, blocks_[index].lease.load(std::memory_order_acquire)};
    return true;
  }

  const uint8_t* readableData(const AudioBlockHandle& handle) const noexcept {
    return owns(handle, AudioBlockOwner::WriterOwned) ? blocks_[handle.index].data : nullptr;
  }

  bool metadata(const AudioBlockHandle& handle, AudioBlockMetadata& metadata) const noexcept {
    if (!owns(handle, AudioBlockOwner::WriterOwned)) return false;
    metadata = blocks_[handle.index].metadata;
    return true;
  }

  bool releaseFromWriter(const AudioBlockHandle& handle) noexcept {
    return returnToFree(handle, AudioBlockOwner::WriterOwned);
  }

  AudioBlockOwner owner(std::size_t index) const noexcept {
    return index < kAudioBlockCount
               ? blocks_[index].owner.load(std::memory_order_acquire)
               : AudioBlockOwner::Uninitialized;
  }

  std::size_t freeDepth() const noexcept { return free_.depth(); }
  std::size_t readyDepth() const noexcept { return ready_.depth(); }
  uint32_t readyHighWatermark() const noexcept {
    return readyHighWatermark_.load(std::memory_order_relaxed);
  }
  uint64_t ownershipErrors() const noexcept {
    return ownershipErrors_.load(std::memory_order_relaxed);
  }

 private:
  struct Block {
    uint8_t* data = nullptr;
    AudioBlockMetadata metadata{};
    std::atomic<uint64_t> lease{0};
    std::atomic<AudioBlockOwner> owner{AudioBlockOwner::Uninitialized};
  };

  bool owns(const AudioBlockHandle& handle, AudioBlockOwner owner) const noexcept {
    return handle.valid() && handle.index < kAudioBlockCount &&
           blocks_[handle.index].lease.load(std::memory_order_acquire) == handle.lease &&
           blocks_[handle.index].owner.load(std::memory_order_acquire) == owner;
  }

  static bool metadataFits(const AudioBlockMetadata& metadata) noexcept {
    const std::size_t bytesPerFrame = metadata.format.bytesPerFrame();
    return metadata.sessionId != 0 && metadata.sequence != 0 && metadata.frameCount != 0 &&
           bytesPerFrame != 0 && metadata.frameCount <= kPcmBytesPerBlock / bytesPerFrame;
  }

  bool returnToFree(const AudioBlockHandle& handle, AudioBlockOwner expectedOwner) noexcept {
    if (!owns(handle, expectedOwner)) {
      noteOwnershipError();
      return false;
    }
    blocks_[handle.index].owner.store(AudioBlockOwner::Free, std::memory_order_release);
    if (!free_.tryPush(handle.index)) {
      noteOwnershipError();
      return false;
    }
    return true;
  }

  void updateReadyHighWatermark() noexcept {
    const uint32_t depth = static_cast<uint32_t>(ready_.depth());
    uint32_t high = readyHighWatermark_.load(std::memory_order_relaxed);
    while (depth > high && !readyHighWatermark_.compare_exchange_weak(
                               high, depth, std::memory_order_relaxed,
                               std::memory_order_relaxed)) {}
  }

  void noteOwnershipError() noexcept { detail::saturatingIncrement(ownershipErrors_); }

  std::array<Block, kAudioBlockCount> blocks_{};
  detail::AudioIndexQueue<kAudioBlockCount> free_{};
  detail::AudioIndexQueue<kMaxReadyBlocks> ready_{};
  uint8_t* storage_ = nullptr;
  std::atomic<bool> initialized_{false};
  std::atomic<uint32_t> readyHighWatermark_{0};
  std::atomic<uint64_t> ownershipErrors_{0};
};

}  // namespace digifant::audio
