#include "../src/audio_block_pool.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>

namespace {

using namespace digifant::audio;

AudioBlockMetadata metadata(uint64_t sequence) {
  return AudioBlockMetadata{7, sequence, (sequence - 1U) * 2048U, sequence * 1000U, 0, 2048,
                            AudioFormat{16000, 1, 16}};
}

void testInitializationAndOwnership() {
  AudioBlockPool pool;
  std::array<uint8_t, kAudioStorageBytes> storage{};
  assert(!pool.initialize(nullptr, storage.size()));
  assert(!pool.initialize(storage.data(), storage.size() - 1U));
  assert(pool.initialize(storage.data(), storage.size()));
  assert(!pool.initialize(storage.data(), storage.size()));
  assert(pool.freeDepth() == kAudioBlockCount);

  AudioBlockHandle mic;
  assert(pool.tryAcquireForMic(mic));
  assert(pool.owner(mic.index) == AudioBlockOwner::MicOwned);
  assert(pool.writableData(mic) != nullptr);
  pool.writableData(mic)[0] = 0x5A;
  assert(pool.publishFromMic(mic, metadata(1)));
  assert(pool.writableData(mic) == nullptr);
  assert(pool.owner(mic.index) == AudioBlockOwner::Ready);

  AudioBlockHandle writer;
  assert(pool.tryAcquireForWriter(writer));
  assert(writer == mic);
  assert(pool.owner(writer.index) == AudioBlockOwner::WriterOwned);
  assert(pool.readableData(writer)[0] == 0x5A);
  AudioBlockMetadata read{};
  assert(pool.metadata(writer, read));
  assert(read.sequence == 1);
  assert(read.frameCount == 2048);
  assert(pool.releaseFromWriter(writer));
  assert(pool.owner(writer.index) == AudioBlockOwner::Free);
  assert(!pool.releaseFromWriter(writer));
  assert(pool.ownershipErrors() == 1);
}

void testReadyLimitAndLeaseProtection() {
  AudioBlockPool pool;
  std::array<uint8_t, kAudioStorageBytes> storage{};
  assert(pool.initialize(storage.data(), storage.size()));
  std::array<AudioBlockHandle, kAudioBlockCount> handles{};
  for (std::size_t i = 0; i < handles.size(); ++i) assert(pool.tryAcquireForMic(handles[i]));
  assert(pool.freeDepth() == 0);
  for (std::size_t i = 0; i < kMaxReadyBlocks; ++i) {
    assert(pool.publishFromMic(handles[i], metadata(i + 1U)));
  }
  assert(!pool.publishFromMic(handles[kMaxReadyBlocks], metadata(kMaxReadyBlocks + 1U)));
  assert(pool.readyDepth() == kMaxReadyBlocks);
  assert(pool.readyHighWatermark() == kMaxReadyBlocks);

  AudioBlockHandle writer;
  assert(pool.tryAcquireForWriter(writer));
  const AudioBlockHandle stale = writer;
  assert(pool.releaseFromWriter(writer));
  AudioBlockHandle reacquired;
  assert(pool.tryAcquireForMic(reacquired));
  assert(reacquired.index == stale.index);
  assert(reacquired.lease != stale.lease);
  assert(pool.writableData(stale) == nullptr);
  assert(!pool.discardFromMic(stale));
  assert(pool.discardFromMic(reacquired));
  assert(pool.discardFromMic(handles[kMaxReadyBlocks]));
  assert(pool.discardFromMic(handles[kMaxReadyBlocks + 1U]));

  while (pool.tryAcquireForWriter(writer)) assert(pool.releaseFromWriter(writer));
  assert(pool.readyDepth() == 0);
  assert(pool.freeDepth() == kAudioBlockCount);
}

void testConcurrentWrapAround() {
  AudioBlockPool pool;
  std::array<uint8_t, kAudioStorageBytes> storage{};
  assert(pool.initialize(storage.data(), storage.size()));
  constexpr uint64_t kIterations = 20000;
  std::atomic<bool> producerDone{false};

  std::thread producer([&] {
    for (uint64_t sequence = 1; sequence <= kIterations; ++sequence) {
      AudioBlockHandle handle;
      while (!pool.tryAcquireForMic(handle)) std::this_thread::yield();
      auto* data = pool.writableData(handle);
      assert(data != nullptr);
      data[0] = static_cast<uint8_t>(sequence);
      while (!pool.publishFromMic(handle, metadata(sequence))) std::this_thread::yield();
    }
    producerDone.store(true, std::memory_order_release);
  });

  uint64_t expected = 1;
  while (!producerDone.load(std::memory_order_acquire) || pool.readyDepth() != 0) {
    AudioBlockHandle handle;
    if (!pool.tryAcquireForWriter(handle)) {
      std::this_thread::yield();
      continue;
    }
    AudioBlockMetadata read{};
    assert(pool.metadata(handle, read));
    assert(read.sequence == expected);
    assert(pool.readableData(handle)[0] == static_cast<uint8_t>(expected));
    ++expected;
    assert(pool.releaseFromWriter(handle));
  }
  producer.join();
  assert(expected == kIterations + 1U);
  assert(pool.freeDepth() == kAudioBlockCount);
  assert(pool.ownershipErrors() == 0);
}

}  // namespace

int main() {
  testInitializationAndOwnership();
  testReadyLimitAndLeaseProtection();
  testConcurrentWrapAround();
  return 0;
}
