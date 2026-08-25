#include "../src/measurement_model.h"

#include <cassert>

int main() {
  digifant::application::MeasurementModel model;
  digifant::transport::KwpFrameEnvelope frame{};
  frame.transportGeneration = 4;
  frame.sessionEpoch = 9;
  frame.rxSequence = 17;
  frame.completedUs = 123456;

  digifant::diagnostic::DiagnosticResult result{};
  result.valid = true;
  result.group000Decoded = true;
  result.group000ValueCount = 10;
  for (uint8_t i = 0; i < 10; ++i) {
    result.group000Values[i].zone = i;
    result.group000Values[i].raw = i;
  }
  result.group000Values[3].raw = 0x3E;
  result.rpm = 420;
  assert(model.apply(frame, result));

  const auto& snapshot = model.snapshot();
  assert(snapshot.fieldCount == 26);
  assert(snapshot.fields[0].group == 0 && snapshot.fields[0].zone == 0);
  assert(snapshot.fields[3].raw == 0x3E && snapshot.fields[3].semantic == digifant::ui::FieldSemantic::MotorLoad);
  assert(snapshot.fields[9].semantic == digifant::ui::FieldSemantic::Rpm);
  assert(snapshot.fields[3].semanticEvidence == digifant::ui::EvidenceGrade::Official);
  assert(snapshot.fields[3].formulaEvidence == digifant::ui::EvidenceGrade::Unknown);
  assert(snapshot.fields[9].formulaEvidence == digifant::ui::EvidenceGrade::Inferred);
  assert(snapshot.fields[3].timestampUs == 123456 && snapshot.fields[3].rxSequence == 17);
  assert(snapshot.fields[25].group == 4 && snapshot.fields[25].zone == 4);

  digifant::diagnostic::DiagnosticResult numbered{};
  numbered.valid = true;
  numbered.decoded = true;
  numbered.group = 4;
  numbered.valueCount = 4;
  numbered.values[2].zone = 3;
  numbered.values[2].formula = 0x87;
  numbered.values[2].nwb = 0x01;
  numbered.values[2].raw = 0x2A;
  assert(model.apply(frame, numbered));
  assert(snapshot.fields[24].raw == 0x2A);
  assert(snapshot.fields[24].status == digifant::ui::FieldStatus::RawOnly);
  assert(snapshot.fields[24].semantic == digifant::ui::FieldSemantic::SpeedSignal);
  assert(snapshot.fields[25].semantic == digifant::ui::FieldSemantic::LoadState);

  digifant::diagnostic::DiagnosticResult group1{};
  group1.valid = true;
  group1.decoded = true;
  group1.group = 1;
  group1.valueCount = 4;
  group1.values[0] = {1, 0x8B, 0x01, true, 950.0f, 0x1A};
  group1.values[1] = {2, 0x8C, 0x08, true, 85.0f, 0x28};
  group1.values[2] = {3, 0x85, 0x80, true, 1.0f, 0x02};
  group1.values[3] = {4, 0x88, 0x01, true, 1.0f, 0xFF};
  assert(model.apply(frame, group1));
  assert(snapshot.fields[10].semantic == digifant::ui::FieldSemantic::Rpm);
  assert(snapshot.fields[11].semantic == digifant::ui::FieldSemantic::Coolant);
  assert(snapshot.fields[12].semantic == digifant::ui::FieldSemantic::LambdaVoltage);
  assert(snapshot.fields[13].semantic == digifant::ui::FieldSemantic::AdjustmentConditions);
  return 0;
}
