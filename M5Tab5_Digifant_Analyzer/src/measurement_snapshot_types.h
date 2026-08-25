#pragma once

#include <array>
#include <cstdint>

namespace digifant::ui {

enum class SignalValidity : uint8_t { Valid, Stale, Disconnected, Unknown, Invalid };
enum class FieldStatus : uint8_t { Unavailable, RawOnly, Decoded, Invalid };
enum class FieldUnit : uint8_t { Unknown, Rpm, Celsius, Volt, Ratio, Percent, Millisecond };
enum class EvidenceGrade : uint8_t { Unknown, Official, Reference, Experimental, Inferred };
enum class FieldSemantic : uint8_t {
  Unknown, Iat, SupplyVoltage, Coolant, MotorLoad, Rpm, LambdaVoltage,
  LambdaTimer, ProbeStatusCounter, ThrottlePotVoltage, InjectionTime,
  ThrottleAngleG69, IdleValveDuty, SpeedSignal, LoadState, AdjustmentConditions,
  Battery, G69
};

struct MeasurementField {
  uint8_t group = 0;
  uint8_t zone = 0;
  uint8_t raw = 0;
  uint8_t formula = 0;
  uint8_t nwb = 0;
  float decodedValue = 0.0f;
  FieldUnit unit = FieldUnit::Unknown;
  FieldSemantic semantic = FieldSemantic::Unknown;
  EvidenceGrade semanticEvidence = EvidenceGrade::Unknown;
  EvidenceGrade formulaEvidence = EvidenceGrade::Unknown;
  FieldStatus status = FieldStatus::Unavailable;
  uint64_t timestampUs = 0;
  uint32_t rxSequence = 0;
  uint32_t sessionEpoch = 0;
  uint32_t transportGeneration = 0;
};

struct MeasurementSnapshot {
  static constexpr uint8_t kFieldCount = 26;
  uint32_t sessionEpoch = 0;
  uint32_t transportGeneration = 0;
  uint32_t lastRxSequence = 0;
  uint64_t lastTimestampUs = 0;
  uint8_t sourceGroup = 0;
  uint8_t sourceZone = 0;
  uint16_t rpm = 0;
  uint8_t coolantRaw = 0;
  uint8_t iatRaw = 0;
  uint8_t batteryRaw = 0;
  uint8_t g69Raw = 0;
  bool k409Connected = false;
  bool kwpConnected = false;
  bool ecuDataValid = false;
  SignalValidity validity = SignalValidity::Unknown;
  uint32_t faultCount = 0;
  uint32_t frameCount = 0;
  uint32_t frameDrops = 0;
  uint32_t rxIngressDrops = 0;
  uint32_t parserRejects = 0;
  uint32_t actionFailures = 0;
  uint32_t snapshotOverwrites = 0;
  uint8_t byteFault = 0;
  uint8_t fieldCount = kFieldCount;
  std::array<MeasurementField, kFieldCount> fields{};
};

}  // namespace digifant::ui
