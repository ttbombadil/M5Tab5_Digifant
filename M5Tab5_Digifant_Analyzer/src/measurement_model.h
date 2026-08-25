#pragma once

#include "diagnostic_decoder.h"
#include "measurement_snapshot_types.h"

namespace digifant::application {

class MeasurementModel {
 public:
  bool apply(const transport::KwpFrameEnvelope& frame,
             const diagnostic::DiagnosticResult& result) noexcept {
    snapshot_.transportGeneration = frame.transportGeneration;
    snapshot_.sessionEpoch = frame.sessionEpoch;
    snapshot_.lastRxSequence = frame.rxSequence;
    snapshot_.lastTimestampUs = frame.completedUs;
    snapshot_.k409Connected = true;
    snapshot_.kwpConnected = true;
    ensureFields();
    if (!result.valid) {
      ++snapshot_.faultCount;
      snapshot_.ecuDataValid = false;
      snapshot_.validity = ui::SignalValidity::Invalid;
      return false;
    }
    if (result.group000Decoded) {
      for (uint8_t i = 0; i < result.group000ValueCount && i < 10; ++i)
        updateField(snapshot_.fields[i], result.group000Values[i], frame);
      snapshot_.fields[9].decodedValue = static_cast<float>(result.rpm);
      snapshot_.fields[9].unit = ui::FieldUnit::Rpm;
      snapshot_.fields[9].formulaEvidence = ui::EvidenceGrade::Inferred;
      snapshot_.fields[9].status = ui::FieldStatus::Decoded;
      snapshot_.iatRaw = result.group000Values[0].raw;
      snapshot_.coolantRaw = result.group000Values[2].raw;
      snapshot_.sourceGroup = 0;
      snapshot_.sourceZone = 9;
      snapshot_.ecuDataValid = true;
      snapshot_.validity = ui::SignalValidity::Valid;
      return true;
    }
    if (!result.decoded) return true;
    for (uint8_t i = 0; i < result.valueCount; ++i) {
      const auto& value = result.values[i];
      auto& field = snapshot_.fields[fieldIndex(result.group, value.zone)];
      updateField(field, value, frame);
      if (result.group == 1 && value.zone == 1 && value.supported) snapshot_.rpm = static_cast<uint16_t>(value.value);
      if (result.group == 1 && value.zone == 2) snapshot_.coolantRaw = value.raw;
      if (result.group == 2 && value.zone == 3) snapshot_.batteryRaw = value.raw;
      if (result.group == 2 && value.zone == 4) snapshot_.iatRaw = value.raw;
      if (result.group == 2 && value.zone == 1 && value.supported) snapshot_.rpm = static_cast<uint16_t>(value.value);
      if (result.group == 3 && value.zone == 1 && value.supported) snapshot_.rpm = static_cast<uint16_t>(value.value);
      if (result.group == 3 && value.zone == 3) snapshot_.g69Raw = value.raw;
      if (result.group == 4 && value.zone == 1 && value.supported) snapshot_.rpm = static_cast<uint16_t>(value.value);
      if (value.supported) {
        snapshot_.sourceGroup = result.group;
        snapshot_.sourceZone = value.zone;
        snapshot_.validity = ui::SignalValidity::Valid;
        snapshot_.ecuDataValid = true;
      }
    }
    return true;
  }

  void onSequenceGap() noexcept {
    ++snapshot_.frameDrops;
    snapshot_.ecuDataValid = false;
    snapshot_.validity = ui::SignalValidity::Stale;
  }

  void onDisconnect() noexcept {
    snapshot_.k409Connected = false;
    snapshot_.kwpConnected = false;
    snapshot_.ecuDataValid = false;
    snapshot_.validity = ui::SignalValidity::Disconnected;
  }

  void onSessionLost() noexcept {
    snapshot_.kwpConnected = false;
    snapshot_.ecuDataValid = false;
    snapshot_.validity = snapshot_.k409Connected ? ui::SignalValidity::Stale
                                                 : ui::SignalValidity::Disconnected;
  }

  void onProtocolStatus(bool k409_connected, bool kwp_connected,
                        uint32_t generation, uint32_t session_epoch) noexcept {
    const bool new_session = kwp_connected &&
      (snapshot_.transportGeneration != generation || snapshot_.sessionEpoch != session_epoch);
    snapshot_.k409Connected = k409_connected;
    snapshot_.kwpConnected = k409_connected && kwp_connected;
    if (k409_connected) snapshot_.transportGeneration = generation;
    if (snapshot_.kwpConnected) snapshot_.sessionEpoch = session_epoch;
    if (!k409_connected) {
      snapshot_.ecuDataValid = false;
      snapshot_.validity = ui::SignalValidity::Disconnected;
    } else if (!kwp_connected) {
      snapshot_.ecuDataValid = false;
      if (snapshot_.validity == ui::SignalValidity::Valid)
        snapshot_.validity = ui::SignalValidity::Stale;
    } else if (new_session) {
      snapshot_.ecuDataValid = false;
      snapshot_.validity = ui::SignalValidity::Unknown;
    }
  }

  bool onRuntimeTelemetry(uint32_t frame_count, uint32_t frame_drops,
                          uint32_t rx_ingress_drops, uint32_t parser_rejects,
                          uint32_t action_failures, uint8_t byte_fault,
                          uint32_t snapshot_overwrites) noexcept {
    const bool changed = snapshot_.frameCount != frame_count ||
      snapshot_.frameDrops != frame_drops || snapshot_.rxIngressDrops != rx_ingress_drops ||
      snapshot_.parserRejects != parser_rejects || snapshot_.actionFailures != action_failures ||
      snapshot_.byteFault != byte_fault || snapshot_.snapshotOverwrites != snapshot_overwrites;
    snapshot_.frameCount = frame_count;
    snapshot_.frameDrops = frame_drops;
    snapshot_.rxIngressDrops = rx_ingress_drops;
    snapshot_.parserRejects = parser_rejects;
    snapshot_.actionFailures = action_failures;
    snapshot_.byteFault = byte_fault;
    snapshot_.snapshotOverwrites = snapshot_overwrites;
    return changed;
  }

  const ui::MeasurementSnapshot& snapshot() const noexcept { return snapshot_; }

 private:
  static uint8_t fieldIndex(uint8_t group, uint8_t zone) noexcept {
    return static_cast<uint8_t>(10U + (group - 1U) * 4U + (zone - 1U));
  }

  static void ensureFieldsIn(ui::MeasurementSnapshot& snapshot) noexcept {
    for (uint8_t i = 0; i < 10; ++i) {
      auto& field = snapshot.fields[i];
      field.group = 0;
      field.zone = i;
      switch (i) {
        case 0: field.semantic = ui::FieldSemantic::Iat; break;
        case 1: field.semantic = ui::FieldSemantic::SupplyVoltage; field.unit = ui::FieldUnit::Volt; break;
        case 2: field.semantic = ui::FieldSemantic::Coolant; field.unit = ui::FieldUnit::Celsius; break;
        case 3: field.semantic = ui::FieldSemantic::MotorLoad; field.unit = ui::FieldUnit::Percent; break;
        case 4: field.semantic = ui::FieldSemantic::LambdaVoltage; field.unit = ui::FieldUnit::Volt; break;
        case 5: field.semantic = ui::FieldSemantic::LambdaTimer; break;
        case 6: field.semantic = ui::FieldSemantic::ProbeStatusCounter; break;
        case 7: field.semantic = ui::FieldSemantic::ThrottlePotVoltage; field.unit = ui::FieldUnit::Volt; break;
        case 8: field.semantic = ui::FieldSemantic::InjectionTime; field.unit = ui::FieldUnit::Millisecond; break;
        case 9: field.semantic = ui::FieldSemantic::Rpm; field.unit = ui::FieldUnit::Rpm; break;
        default: break;
      }
      field.semanticEvidence = ui::EvidenceGrade::Official;
      if (i == 1 || i == 3 || i == 7 || i == 8 || i == 9)
        field.formulaEvidence = ui::EvidenceGrade::Inferred;
    }
    for (uint8_t group = 1; group <= 4; ++group) {
      for (uint8_t zone = 1; zone <= 4; ++zone) {
        auto& field = snapshot.fields[fieldIndex(group, zone)];
        field.group = group;
        field.zone = zone;
        field.semanticEvidence = ui::EvidenceGrade::Official;
        if (group == 1 && zone == 1) { field.semantic = ui::FieldSemantic::Rpm; field.unit = ui::FieldUnit::Rpm; }
        if (group == 1 && zone == 2) { field.semantic = ui::FieldSemantic::Coolant; field.unit = ui::FieldUnit::Celsius; }
        if (group == 1 && zone == 3) { field.semantic = ui::FieldSemantic::LambdaVoltage; field.unit = ui::FieldUnit::Volt; }
        if (group == 1 && zone == 4) field.semantic = ui::FieldSemantic::AdjustmentConditions;
        if (group == 2 && zone == 1) { field.semantic = ui::FieldSemantic::Rpm; field.unit = ui::FieldUnit::Rpm; }
        if (group == 2 && zone == 2) { field.semantic = ui::FieldSemantic::InjectionTime; field.unit = ui::FieldUnit::Millisecond; }
        if (group == 2 && zone == 3) { field.semantic = ui::FieldSemantic::SupplyVoltage; field.unit = ui::FieldUnit::Volt; }
        if (group == 2 && zone == 4) { field.semantic = ui::FieldSemantic::Iat; field.unit = ui::FieldUnit::Celsius; }
        if (group == 3 && zone == 1) { field.semantic = ui::FieldSemantic::Rpm; field.unit = ui::FieldUnit::Rpm; }
        if (group == 3 && zone == 2) { field.semantic = ui::FieldSemantic::MotorLoad; field.unit = ui::FieldUnit::Percent; }
        if (group == 3 && zone == 3) field.semantic = ui::FieldSemantic::ThrottleAngleG69;
        if (group == 3 && zone == 4) { field.semantic = ui::FieldSemantic::IdleValveDuty; field.unit = ui::FieldUnit::Percent; }
        if (group == 4 && zone == 1) { field.semantic = ui::FieldSemantic::Rpm; field.unit = ui::FieldUnit::Rpm; }
        if (group == 4 && zone == 2) { field.semantic = ui::FieldSemantic::MotorLoad; field.unit = ui::FieldUnit::Percent; }
        if (group == 4 && zone == 3) field.semantic = ui::FieldSemantic::SpeedSignal;
        if (group == 4 && zone == 4) field.semantic = ui::FieldSemantic::LoadState;
      }
    }
    snapshot.fieldCount = ui::MeasurementSnapshot::kFieldCount;
  }

  static void updateField(ui::MeasurementField& field, const diagnostic::DiagnosticValue& value,
                          const transport::KwpFrameEnvelope& frame) noexcept {
    field.raw = value.raw;
    field.formula = value.formula;
    field.nwb = value.nwb;
    field.decodedValue = value.value;
    field.status = value.supported ? ui::FieldStatus::Decoded : ui::FieldStatus::RawOnly;
    field.formulaEvidence = formulaEvidence(value.formula);
    field.timestampUs = frame.completedUs;
    field.rxSequence = frame.rxSequence;
    field.sessionEpoch = frame.sessionEpoch;
    field.transportGeneration = frame.transportGeneration;
  }

  static ui::EvidenceGrade formulaEvidence(uint8_t formula) noexcept {
    switch (formula) {
      case 0x8B: case 0x8C: case 0x85: case 0x88: case 0x89:
        return ui::EvidenceGrade::Reference;
      default:
        return ui::EvidenceGrade::Unknown;
    }
  }

  void ensureFields() noexcept {
    if (snapshot_.fields[3].semantic == ui::FieldSemantic::Unknown) ensureFieldsIn(snapshot_);
  }

  ui::MeasurementSnapshot snapshot_{};
};

}  // namespace digifant::application
