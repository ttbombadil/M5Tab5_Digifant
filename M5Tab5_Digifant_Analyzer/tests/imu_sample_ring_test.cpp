#include "../src/imu_sample_ring.h"

#include <cassert>

int main() {
  digifant::imu::ImuSampleRing ring;
  digifant::imu::ImuSample sample{};
  for (uint32_t i = 0; i < digifant::imu::ImuSampleRing::kCapacity; ++i) {
    sample.sequence = i;
    assert(ring.tryPush(sample));
  }
  sample.sequence = 256;
  assert(!ring.tryPush(sample));
  assert(ring.drops() == 1);
  assert(ring.highWatermark() == 256);
  for (uint32_t i = 0; i < 100; ++i) {
    assert(ring.tryPop(sample));
    assert(sample.sequence == i);
  }
  for (uint32_t i = 256; i < 356; ++i) {
    sample.sequence = i;
    assert(ring.tryPush(sample));
  }
  for (uint32_t i = 100; i < 356; ++i) {
    assert(ring.tryPop(sample));
    assert(sample.sequence == i);
  }
  assert(!ring.tryPop(sample));

  digifant::imu::ImuDiagnosticsMailbox diagnostics;
  for (uint32_t i = 1; i <= 300; ++i) {
    sample.sequence = i;
    diagnostics.publish(sample);
  }
  assert(diagnostics.accepted() == 300);
  digifant::imu::ImuSample latest{};
  assert(diagnostics.receive(latest));
  assert(latest.sequence == 300);
  assert(!diagnostics.receive(latest));
  return 0;
}
