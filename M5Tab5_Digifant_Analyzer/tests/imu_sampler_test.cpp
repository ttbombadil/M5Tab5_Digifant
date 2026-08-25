#include "../src/imu_sampler.h"

#include <cassert>
#include <cstdint>
#include <type_traits>

using namespace digifant::imu;

struct FakeSource final : IImuSource {
  bool valid = true;
  bool read(ImuReading& reading) noexcept override {
    reading.accelXG = 1.25F;
    reading.accelYG = -0.5F;
    reading.accelZG = 0.0F;
    reading.gyroXDps = 2.0F;
    reading.gyroYDps = -3.0F;
    reading.gyroZDps = 4.0F;
    reading.accelValid = valid;
    reading.gyroValid = valid;
    return valid;
  }
};

struct FakeSink final : IImuSink {
  ImuSample samples[8]{};
  uint32_t count = 0;
  bool accept = true;
  bool tryPublish(const ImuSample& sample) noexcept override {
    if (!accept) return false;
    samples[count++] = sample;
    return true;
  }
};

int main() {
  static_assert(std::is_trivially_copyable_v<ImuSample>);
  static_assert(sizeof(ImuSample) <= 48);
  FakeSource source;
  FakeSink sink;
  ImuSampler sampler(source, sink);

  assert(sampler.poll(1'000'000));
  assert(!sampler.poll(1'000'001));
  assert(sampler.poll(1'040'000));
  assert(sink.count == 2);
  assert(sink.samples[0].timestampUs == 1'000'000);
  assert(sink.samples[1].sequence == 1);
  assert(sink.samples[0].accelXMg == 1250);
  assert(sink.samples[0].gyroYMdps == -3000);

  // A late wake skips missed periods and emits at most one sample.
  assert(sampler.poll(1'250'000));
  assert(sink.count == 3);
  assert(sampler.nextDeadlineUs() == 1'280'000);

  source.valid = false;
  assert(sampler.poll(1'280'000));
  assert(sink.samples[3].validity == 0);

  sink.accept = false;
  assert(!sampler.poll(1'320'000));
  assert(sampler.nextSequence() == 5);
  return 0;
}
