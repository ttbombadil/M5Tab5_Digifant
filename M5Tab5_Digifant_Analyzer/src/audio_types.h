#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace digifant::audio {

constexpr std::size_t kPcmBytesPerBlock = 4096;
constexpr std::size_t kAudioBlockCount = 34;
constexpr std::size_t kMaxReadyBlocks = 32;
constexpr std::size_t kAudioStorageBytes = kPcmBytesPerBlock * kAudioBlockCount;
constexpr uint16_t kInvalidBlockIndex = std::numeric_limits<uint16_t>::max();

struct AudioFormat {
  uint32_t sampleRateHz = 0;
  uint8_t channelCount = 0;
  uint8_t bitsPerSample = 0;

  constexpr bool valid() const noexcept {
    return sampleRateHz != 0 && channelCount != 0 && bitsPerSample != 0 &&
           bitsPerSample % 8U == 0;
  }

  constexpr std::size_t bytesPerFrame() const noexcept {
    return valid() ? static_cast<std::size_t>(channelCount) * (bitsPerSample / 8U) : 0U;
  }
};

constexpr bool operator==(const AudioFormat& lhs, const AudioFormat& rhs) noexcept {
  return lhs.sampleRateHz == rhs.sampleRateHz && lhs.channelCount == rhs.channelCount &&
         lhs.bitsPerSample == rhs.bitsPerSample;
}

struct AudioBlockHandle {
  uint16_t index = kInvalidBlockIndex;
  uint64_t lease = 0;

  constexpr bool valid() const noexcept {
    return index != kInvalidBlockIndex && lease != 0;
  }
};

constexpr bool operator==(const AudioBlockHandle& lhs, const AudioBlockHandle& rhs) noexcept {
  return lhs.index == rhs.index && lhs.lease == rhs.lease;
}

struct AudioBlockMetadata {
  uint64_t sessionId = 0;
  uint64_t sequence = 0;
  uint64_t firstFrameIndex = 0;
  uint64_t firstFrameAtUs = 0;
  uint64_t gapFramesBefore = 0;
  uint32_t frameCount = 0;
  AudioFormat format{};
};

enum class AudioBlockOwner : uint8_t {
  Uninitialized,
  Free,
  MicOwned,
  Ready,
  WriterOwned,
};

enum class AudioSamplerState : uint8_t {
  Idle,
  Recording,
};

enum class AudioAcquireResult : uint8_t {
  Acquired,
  NotRecording,
  InFlightLimit,
  NoFreeBlock,
};

enum class AudioCommitResult : uint8_t {
  Published,
  NotRecording,
  UnknownHandle,
  InvalidFrameCount,
  ReadyQueueFull,
};

struct AudioSamplerStatus {
  AudioSamplerState state = AudioSamplerState::Idle;
  uint64_t sessionId = 0;
  uint64_t nextFrameIndex = 0;
  uint64_t pendingGapFrames = 0;
  uint64_t publishedBlocks = 0;
  uint64_t publishedFrames = 0;
  uint64_t droppedBlocks = 0;
  uint64_t droppedFrames = 0;
  uint64_t sourceErrors = 0;
  uint64_t rejectedStarts = 0;
  uint64_t rejectedOperations = 0;
  uint64_t abortedBlocks = 0;
  uint32_t inFlightBlocks = 0;
};

}  // namespace digifant::audio
