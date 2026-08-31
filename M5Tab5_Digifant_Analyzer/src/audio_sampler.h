#pragma once

#include "audio_block_pool.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace digifant::audio {

class AudioSampler {
 public:
  static constexpr std::size_t kMaxInFlightBlocks = 2;

  explicit AudioSampler(AudioBlockPool& pool) noexcept : pool_(pool) {}

  bool start(uint64_t sessionId, const AudioFormat& format) noexcept {
    if (state_ != AudioSamplerState::Idle || !pool_.initialized() || sessionId == 0 ||
        !format.valid() ||
        format.bytesPerFrame() > kPcmBytesPerBlock) {
      saturatingIncrement(status_.rejectedStarts);
      return false;
    }
    state_ = AudioSamplerState::Recording;
    sessionId_ = sessionId;
    format_ = format;
    nextFrameIndex_ = 0;
    pendingGapFrames_ = 0;
    nextSequence_ = 1;
    inFlightCount_ = 0;
    for (auto& handle : inFlight_) handle = {};
    publishStateToStatus();
    return true;
  }

  void stop() noexcept {
    if (state_ == AudioSamplerState::Idle) return;
    for (auto& handle : inFlight_) {
      if (!handle.valid()) continue;
      if (pool_.discardFromMic(handle)) saturatingIncrement(status_.abortedBlocks);
      handle = {};
    }
    inFlightCount_ = 0;
    state_ = AudioSamplerState::Idle;
    pendingGapFrames_ = 0;
    publishStateToStatus();
  }

  AudioAcquireResult tryBeginBlock(AudioBlockHandle& handle) noexcept {
    handle = {};
    if (state_ != AudioSamplerState::Recording) {
      saturatingIncrement(status_.rejectedOperations);
      return AudioAcquireResult::NotRecording;
    }
    if (inFlightCount_ >= kMaxInFlightBlocks) {
      saturatingIncrement(status_.rejectedOperations);
      return AudioAcquireResult::InFlightLimit;
    }
    if (!pool_.tryAcquireForMic(handle)) return AudioAcquireResult::NoFreeBlock;
    for (auto& slot : inFlight_) {
      if (!slot.valid()) {
        slot = handle;
        ++inFlightCount_;
        publishStateToStatus();
        return AudioAcquireResult::Acquired;
      }
    }
    (void)pool_.discardFromMic(handle);
    handle = {};
    saturatingIncrement(status_.rejectedOperations);
    return AudioAcquireResult::InFlightLimit;
  }

  uint8_t* writableData(const AudioBlockHandle& handle) noexcept {
    return isTracked(handle) ? pool_.writableData(handle) : nullptr;
  }

  AudioCommitResult commitBlock(const AudioBlockHandle& handle, uint32_t frameCount,
                                uint64_t firstFrameAtUs) noexcept {
    if (state_ != AudioSamplerState::Recording) {
      saturatingIncrement(status_.rejectedOperations);
      return AudioCommitResult::NotRecording;
    }
    if (!isTracked(handle)) {
      saturatingIncrement(status_.rejectedOperations);
      return AudioCommitResult::UnknownHandle;
    }
    const std::size_t bytesPerFrame = format_.bytesPerFrame();
    if (frameCount == 0 || bytesPerFrame == 0 ||
        frameCount > kPcmBytesPerBlock / bytesPerFrame) {
      saturatingIncrement(status_.rejectedOperations);
      return AudioCommitResult::InvalidFrameCount;
    }

    AudioBlockMetadata metadata{};
    metadata.sessionId = sessionId_;
    metadata.sequence = nextSequence_;
    metadata.firstFrameIndex = nextFrameIndex_;
    metadata.firstFrameAtUs = firstFrameAtUs;
    metadata.gapFramesBefore = pendingGapFrames_;
    metadata.frameCount = frameCount;
    metadata.format = format_;

    advance(nextSequence_, 1);
    if (!pool_.publishFromMic(handle, metadata)) {
      (void)pool_.discardFromMic(handle);
      removeTracked(handle);
      noteDroppedBlock(frameCount);
      publishStateToStatus();
      return AudioCommitResult::ReadyQueueFull;
    }

    removeTracked(handle);
    advance(nextFrameIndex_, frameCount);
    pendingGapFrames_ = 0;
    saturatingIncrement(status_.publishedBlocks);
    saturatingAdd(status_.publishedFrames, frameCount);
    publishStateToStatus();
    return AudioCommitResult::Published;
  }

  bool discardBlock(const AudioBlockHandle& handle, uint32_t frameCount,
                    bool sourceError) noexcept {
    if (state_ != AudioSamplerState::Recording || !isTracked(handle)) {
      saturatingIncrement(status_.rejectedOperations);
      return false;
    }
    if (!pool_.discardFromMic(handle)) {
      saturatingIncrement(status_.rejectedOperations);
      return false;
    }
    removeTracked(handle);
    advance(nextSequence_, 1);
    noteDroppedBlock(frameCount);
    if (sourceError) saturatingIncrement(status_.sourceErrors);
    publishStateToStatus();
    return true;
  }

  bool recordDroppedFrames(uint32_t frameCount, bool sourceError = false) noexcept {
    if (state_ != AudioSamplerState::Recording || frameCount == 0) {
      saturatingIncrement(status_.rejectedOperations);
      return false;
    }
    advance(nextSequence_, 1);
    noteDroppedBlock(frameCount);
    if (sourceError) saturatingIncrement(status_.sourceErrors);
    publishStateToStatus();
    return true;
  }

  AudioSamplerStatus status() const noexcept {
    AudioSamplerStatus result = status_;
    result.state = state_;
    result.sessionId = sessionId_;
    result.nextFrameIndex = nextFrameIndex_;
    result.pendingGapFrames = pendingGapFrames_;
    result.inFlightBlocks = static_cast<uint32_t>(inFlightCount_);
    return result;
  }

 private:
  static void saturatingIncrement(uint64_t& value) noexcept { saturatingAdd(value, 1); }

  static void saturatingAdd(uint64_t& value, uint64_t amount) noexcept {
    value = amount > std::numeric_limits<uint64_t>::max() - value
                ? std::numeric_limits<uint64_t>::max()
                : value + amount;
  }

  static void advance(uint64_t& value, uint64_t amount) noexcept {
    saturatingAdd(value, amount);
  }

  bool isTracked(const AudioBlockHandle& handle) const noexcept {
    for (const auto& slot : inFlight_) {
      if (slot == handle) return true;
    }
    return false;
  }

  void removeTracked(const AudioBlockHandle& handle) noexcept {
    for (auto& slot : inFlight_) {
      if (!(slot == handle)) continue;
      slot = {};
      if (inFlightCount_ != 0) --inFlightCount_;
      return;
    }
  }

  void noteDroppedBlock(uint32_t frameCount) noexcept {
    saturatingIncrement(status_.droppedBlocks);
    saturatingAdd(status_.droppedFrames, frameCount);
    advance(nextFrameIndex_, frameCount);
    advance(pendingGapFrames_, frameCount);
  }

  void publishStateToStatus() noexcept {
    status_.state = state_;
    status_.sessionId = sessionId_;
    status_.nextFrameIndex = nextFrameIndex_;
    status_.pendingGapFrames = pendingGapFrames_;
    status_.inFlightBlocks = static_cast<uint32_t>(inFlightCount_);
  }

  AudioBlockPool& pool_;
  AudioSamplerState state_ = AudioSamplerState::Idle;
  uint64_t sessionId_ = 0;
  AudioFormat format_{};
  uint64_t nextFrameIndex_ = 0;
  uint64_t pendingGapFrames_ = 0;
  uint64_t nextSequence_ = 1;
  std::array<AudioBlockHandle, kMaxInFlightBlocks> inFlight_{};
  std::size_t inFlightCount_ = 0;
  AudioSamplerStatus status_{};
};

}  // namespace digifant::audio
