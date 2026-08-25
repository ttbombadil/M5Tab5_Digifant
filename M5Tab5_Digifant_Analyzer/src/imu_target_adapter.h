#pragma once

#include "imu_sampler.h"

#include <M5Unified.h>

namespace digifant::imu {

class M5Tab5ImuSource final : public IImuSource {
 public:
  bool read(ImuReading& reading) noexcept override {
    const auto available = M5.Imu.update();
    const bool haveAccel = (available & m5::IMU_Class::sensor_mask_accel) != 0;
    const bool haveGyro = (available & m5::IMU_Class::sensor_mask_gyro) != 0;
    const auto& data = M5.Imu.getImuData();
    reading.accelXG = data.accel.x;
    reading.accelYG = data.accel.y;
    reading.accelZG = data.accel.z;
    reading.gyroXDps = data.gyro.x;
    reading.gyroYDps = data.gyro.y;
    reading.gyroZDps = data.gyro.z;
    reading.accelValid = haveAccel;
    reading.gyroValid = haveGyro;
    return haveAccel || haveGyro;
  }
};

}  // namespace digifant::imu
