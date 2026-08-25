#include <cassert>

#include "../src/display_ui_model.h"

int main() {
  using namespace digifant::ui;
  DisplayUiModel model;
  MeasurementSnapshot snapshot{};
  snapshot.fieldCount = MeasurementSnapshot::kFieldCount;
  snapshot.validity = SignalValidity::Valid;
  snapshot.lastRxSequence = 7;
  snapshot.transportGeneration = 2;
  snapshot.sessionEpoch = 3;
  snapshot.rpm = 900;
  snapshot.coolantRaw = 42;
  snapshot.iatRaw = 55;
  snapshot.batteryRaw = 150;
  snapshot.k409Connected = true;
  snapshot.kwpConnected = true;
  snapshot.ecuDataValid = true;
  snapshot.fields[10].group = 1;
  snapshot.fields[10].zone = 1;
  snapshot.fields[10].status = FieldStatus::Decoded;
  snapshot.fields[10].decodedValue = 910.0f;
  snapshot.fields[10].semantic = FieldSemantic::Rpm;
  snapshot.fields[10].formulaEvidence = EvidenceGrade::Reference;
  snapshot.fields[10].transportGeneration = 2;
  snapshot.fields[10].sessionEpoch = 3;
  snapshot.fields[10].rxSequence = 6;
  snapshot.fields[12].group = 1;
  snapshot.fields[12].zone = 3;
  snapshot.fields[12].raw = 100;
  snapshot.fields[12].status = FieldStatus::RawOnly;
  snapshot.fields[12].semantic = FieldSemantic::LambdaVoltage;
  snapshot.fields[12].formulaEvidence = EvidenceGrade::Unknown;
  snapshot.fields[12].transportGeneration = 2;
  snapshot.fields[12].sessionEpoch = 3;
  assert(displayFieldStyle(snapshot.fields[10], SignalValidity::Valid) == DisplayFieldStyle::Normal);
  assert(displayFieldStyle(snapshot.fields[12], SignalValidity::Valid) == DisplayFieldStyle::RawUnknown);
  snapshot.fields[10].formulaEvidence = EvidenceGrade::Inferred;
  assert(displayFieldStyle(snapshot.fields[10], SignalValidity::Valid) == DisplayFieldStyle::Caution);
  snapshot.fields[10].formulaEvidence = EvidenceGrade::Reference;
  assert(displayFieldStyle(snapshot.fields[10], SignalValidity::Stale) == DisplayFieldStyle::Invalid);
  snapshot.fields[10].status = FieldStatus::Invalid;
  assert(displayFieldStyle(snapshot.fields[10], SignalValidity::Valid) == DisplayFieldStyle::Invalid);
  snapshot.fields[10].status = FieldStatus::Decoded;
  model.accept(snapshot);
  assert(model.hasSnapshot());
  assert(model.field(1, 1) != nullptr);
  assert(model.preferredRpmField() == model.field(1, 1));
  assert(model.engineOperatingState() == EngineOperatingState::Running);
  assert(model.compactAvailability(FieldSemantic::InjectionTime) ==
         CompactFieldAvailability::Available);
  assert(model.compactAvailability(FieldSemantic::MotorLoad) ==
         CompactFieldAvailability::Available);
  assert(model.compactAvailability(FieldSemantic::Coolant) ==
         CompactFieldAvailability::Available);

  MeasurementSnapshot engineStopped = snapshot;
  engineStopped.lastRxSequence = 8;
  engineStopped.fields[10].decodedValue = 0.0f;
  engineStopped.fields[10].rxSequence = 8;
  model.accept(engineStopped);
  assert(model.engineOperatingState() == EngineOperatingState::Stopped);
  assert(model.compactAvailability(FieldSemantic::InjectionTime) ==
         CompactFieldAvailability::EngineStopped);
  assert(model.compactAvailability(FieldSemantic::MotorLoad) ==
         CompactFieldAvailability::EngineStopped);
  assert(model.compactAvailability(FieldSemantic::Iat) ==
         CompactFieldAvailability::Available);
  model.accept(snapshot);
  model.clearDirty();
  MeasurementSnapshot lostKwp = snapshot;
  lostKwp.kwpConnected = false;
  lostKwp.ecuDataValid = false;
  lostKwp.validity = SignalValidity::Stale;
  model.accept(lostKwp);
  assert(model.dirty());
  model.accept(snapshot);
  MeasurementSnapshot nextSession = snapshot;
  nextSession.sessionEpoch = 4;
  nextSession.lastRxSequence = 8;
  model.accept(nextSession);
  assert(model.preferredRpmField() == nullptr);
  assert(model.engineOperatingState() == EngineOperatingState::Unknown);
  assert(model.compactAvailability(FieldSemantic::InjectionTime) ==
         CompactFieldAvailability::EngineStateUnknown);
  assert(displayFieldStyle(nextSession.fields[10], nextSession) == DisplayFieldStyle::Invalid);
  model.accept(snapshot);
  assert(model.tab() == DisplayTab::Compact);
  model.setTab(DisplayTab::List);
  model.setListPage(1);
  assert(model.listPage() == 1);
  model.setTab(DisplayTab::Traces);
  model.sampleIfDue(100);
  assert(model.scope().size() == 1);
  model.toggleScopePause();
  model.sampleIfDue(300);
  assert(model.scope().size() == 1);
  model.toggleScopePause();
  model.sampleIfDue(400);
  assert(model.scope().size() == 2);

  DisplayScopeRing<2> ring;
  ring.push({1}); ring.push({2}); ring.push({3});
  assert(ring.size() == 2);
  assert(ring.chronological(0).rpm == 2);
  assert(ring.chronological(1).rpm == 3);
  return 0;
}
