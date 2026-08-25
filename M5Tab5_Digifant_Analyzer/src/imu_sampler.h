#pragma once

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace digifant::imu {

// Native sensor axes are deliberately retained.  No vehicle-axis or driving
// semantics belong in this observation path.
struct ImuSample {
  uint64_t timestampUs = 0;
  uint32_t sequence = 0;
  int32_t accelXMg = 0;
  int32_t accelYMg = 0;
  int32_t accelZMg = 0;
  int32_t gyroXMdps = 0;
  int32_t gyroYMdps = 0;
  int32_t gyroZMdps = 0;
  uint8_t validity = 0;
};

static_assert(std::is_trivially_copyable_v<ImuSample>);
static_assert(sizeof(ImuSample) <= 48);

enum ImuValidity : uint8_t {
  kReadValid = 1U << 0,
  kAccelValid = 1U << 1,
  kGyroValid = 1U << 2,
};

struct ImuReading {
  float accelXG = 0.0F;
  float accelYG = 0.0F;
  float accelZG = 0.0F;
  float gyroXDps = 0.0F;
  float gyroYDps = 0.0F;
  float gyroZDps = 0.0F;
  bool accelValid = false;
  bool gyroValid = false;
};

class IImuSource {
 public:
  virtual ~IImuSource() = default;
  virtual bool read(ImuReading& reading) noexcept = 0;
};

class IImuSink {
 public:
  virtual ~IImuSink() = default;
  virtual bool tryPublish(const ImuSample& sample) noexcept = 0;
};

class ImuSampler {
 public:
  static constexpr uint64_t kPeriodUs = 40'000;

  ImuSampler(IImuSource& source, IImuSink& sink) noexcept : source_(source), sink_(sink) {}

  // Call from the sampler task whenever it wakes.  Deadlines are absolute;
  // missed periods are skipped instead of being replayed in an unbounded loop.
  bool poll(uint64_t nowUs) noexcept {
    if (!started_) {
      started_ = true;
      nextDeadlineUs_ = nowUs;
    }
    if (nowUs < nextDeadlineUs_) return false;

    ImuReading reading{};
    const bool readOk = source_.read(reading);
    ImuSample sample{};
    sample.timestampUs = nowUs;
    sample.sequence = sequence_++;
    sample.validity = readOk ? kReadValid : 0U;
    if (readOk && reading.accelValid) {
      sample.accelXMg = toMilli(reading.accelXG);
      sample.accelYMg = toMilli(reading.accelYG);
      sample.accelZMg = toMilli(reading.accelZG);
      sample.validity = static_cast<uint8_t>(sample.validity | kAccelValid);
    }
    if (readOk && reading.gyroValid) {
      sample.gyroXMdps = toMilli(reading.gyroXDps);
      sample.gyroYMdps = toMilli(reading.gyroYDps);
      sample.gyroZMdps = toMilli(reading.gyroZDps);
      sample.validity = static_cast<uint8_t>(sample.validity | kGyroValid);
    }
    const bool published = sink_.tryPublish(sample);

    nextDeadlineUs_ += kPeriodUs;
    if (nextDeadlineUs_ <= nowUs) {
      const uint64_t latePeriods = (nowUs - nextDeadlineUs_) / kPeriodUs + 1U;
      nextDeadlineUs_ += latePeriods * kPeriodUs;
    }
    return published;
  }

  uint64_t nextDeadlineUs() const noexcept { return nextDeadlineUs_; }
  uint32_t nextSequence() const noexcept { return sequence_; }

 private:
  static int32_t toMilli(float value) noexcept {
    return std::isfinite(value) ? static_cast<int32_t>(std::lround(value * 1000.0F)) : 0;
  }

  IImuSource& source_;
  IImuSink& sink_;
  bool started_ = false;
  uint64_t nextDeadlineUs_ = 0;
  uint32_t sequence_ = 0;
};

}  // namespace digifant::imu
