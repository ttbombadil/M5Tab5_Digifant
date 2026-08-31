#include "../src/audio_sampler.h"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

using namespace digifant::audio;

void drain(AudioBlockPool& pool) {
  AudioBlockHandle handle;
  while (pool.tryAcquireForWriter(handle)) assert(pool.releaseFromWriter(handle));
}

void testLifecycleAndGapSemantics() {
  AudioBlockPool pool;
  std::array<uint8_t, kAudioStorageBytes> storage{};
  AudioSampler sampler(pool);
  const AudioFormat mono{16000, 1, 16};
  assert(!sampler.start(1, mono));
  assert(pool.initialize(storage.data(), storage.size()));

  AudioBlockHandle handle;
  assert(sampler.tryBeginBlock(handle) == AudioAcquireResult::NotRecording);
  assert(!sampler.start(0, mono));
  assert(!sampler.start(1, AudioFormat{}));
  assert(sampler.start(42, mono));
  assert(!sampler.start(43, mono));
  assert(sampler.tryBeginBlock(handle) == AudioAcquireResult::Acquired);
  assert(sampler.writableData(handle) != nullptr);
  assert(sampler.commitBlock(handle, 2048, 100000) == AudioCommitResult::Published);

  assert(sampler.recordDroppedFrames(2048));
  assert(sampler.tryBeginBlock(handle) == AudioAcquireResult::Acquired);
  assert(sampler.commitBlock(handle, 1024, 356000) == AudioCommitResult::Published);

  AudioBlockHandle writer;
  AudioBlockMetadata first{};
  AudioBlockMetadata second{};
  assert(pool.tryAcquireForWriter(writer));
  assert(pool.metadata(writer, first));
  assert(pool.releaseFromWriter(writer));
  assert(pool.tryAcquireForWriter(writer));
  assert(pool.metadata(writer, second));
  assert(pool.releaseFromWriter(writer));
  assert(first.sessionId == 42);
  assert(first.sequence == 1);
  assert(first.firstFrameIndex == 0);
  assert(first.gapFramesBefore == 0);
  assert(second.sequence == 3);
  assert(second.firstFrameIndex == 4096);
  assert(second.gapFramesBefore == 2048);

  const AudioSamplerStatus running = sampler.status();
  assert(running.state == AudioSamplerState::Recording);
  assert(running.nextFrameIndex == 5120);
  assert(running.pendingGapFrames == 0);
  assert(running.publishedBlocks == 2);
  assert(running.publishedFrames == 3072);
  assert(running.droppedBlocks == 1);
  assert(running.droppedFrames == 2048);

  sampler.stop();
  assert(sampler.status().state == AudioSamplerState::Idle);
  assert(sampler.start(43, AudioFormat{16000, 2, 16}));
  assert(sampler.status().nextFrameIndex == 0);
  sampler.stop();
}

void testInFlightAndSourceError() {
  AudioBlockPool pool;
  std::array<uint8_t, kAudioStorageBytes> storage{};
  assert(pool.initialize(storage.data(), storage.size()));
  AudioSampler sampler(pool);
  assert(sampler.start(1, AudioFormat{16000, 2, 16}));

  AudioBlockHandle first;
  AudioBlockHandle second;
  AudioBlockHandle third;
  assert(sampler.tryBeginBlock(first) == AudioAcquireResult::Acquired);
  assert(sampler.tryBeginBlock(second) == AudioAcquireResult::Acquired);
  assert(sampler.tryBeginBlock(third) == AudioAcquireResult::InFlightLimit);
  assert(sampler.commitBlock(first, 2048, 1) == AudioCommitResult::InvalidFrameCount);
  assert(sampler.discardBlock(first, 512, true));
  assert(sampler.commitBlock(second, 1024, 2) == AudioCommitResult::Published);

  const AudioSamplerStatus status = sampler.status();
  assert(status.sourceErrors == 1);
  assert(status.droppedBlocks == 1);
  assert(status.droppedFrames == 512);
  assert(status.publishedBlocks == 1);
  assert(status.nextFrameIndex == 1536);

  assert(sampler.tryBeginBlock(first) == AudioAcquireResult::Acquired);
  sampler.stop();
  assert(sampler.status().abortedBlocks == 1);
  assert(pool.owner(first.index) == AudioBlockOwner::Free);
  drain(pool);
  assert(pool.freeDepth() == kAudioBlockCount);
}

void testReadyQueueOverflowIsExplicitGap() {
  AudioBlockPool pool;
  std::array<uint8_t, kAudioStorageBytes> storage{};
  assert(pool.initialize(storage.data(), storage.size()));
  AudioSampler sampler(pool);
  assert(sampler.start(9, AudioFormat{16000, 1, 16}));

  for (uint64_t i = 0; i < kMaxReadyBlocks; ++i) {
    AudioBlockHandle handle;
    assert(sampler.tryBeginBlock(handle) == AudioAcquireResult::Acquired);
    assert(sampler.commitBlock(handle, 2048, 1000U + i) == AudioCommitResult::Published);
  }
  AudioBlockHandle overflow;
  assert(sampler.tryBeginBlock(overflow) == AudioAcquireResult::Acquired);
  assert(sampler.commitBlock(overflow, 2048, 99999) == AudioCommitResult::ReadyQueueFull);
  assert(sampler.status().pendingGapFrames == 2048);

  AudioBlockHandle writer;
  assert(pool.tryAcquireForWriter(writer));
  assert(pool.releaseFromWriter(writer));
  AudioBlockHandle afterGap;
  assert(sampler.tryBeginBlock(afterGap) == AudioAcquireResult::Acquired);
  assert(sampler.commitBlock(afterGap, 1024, 120000) == AudioCommitResult::Published);

  AudioBlockMetadata last{};
  while (pool.tryAcquireForWriter(writer)) {
    assert(pool.metadata(writer, last));
    assert(pool.releaseFromWriter(writer));
  }
  assert(last.sequence == 34);
  assert(last.firstFrameIndex == 67584);
  assert(last.gapFramesBefore == 2048);
  assert(sampler.status().droppedBlocks == 1);
  assert(sampler.status().droppedFrames == 2048);
  sampler.stop();
}

}  // namespace

int main() {
  testLifecycleAndGapSemantics();
  testInFlightAndSourceError();
  testReadyQueueOverflowIsExplicitGap();
  return 0;
}
