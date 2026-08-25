#include "../src/validated_frame_queue.h"
#include <cassert>

using namespace digifant::transport;

int main() {
  ValidatedFrameQueue frames;
  KwpFrameEnvelope input{};
  input.size = 4;
  input.bytes[0] = 3;
  input.bytes[3] = 3;
  for (uint32_t i = 0; i < 32; ++i) assert(frames.trySend(input));
  assert(!frames.trySend(input));
  assert(frames.drops() == 1 && frames.highWatermark() == 32);
  KwpFrameEnvelope out{};
  for (uint32_t i = 0; i < 32; ++i) {
    assert(frames.tryReceive(out));
    assert(out.rxSequence == i);
  }
  assert(!frames.tryReceive(out));
  assert(frames.trySend(input));
  assert(frames.tryReceive(out) && out.rxSequence == 33);  // 32 was dropped.
  return 0;
}
