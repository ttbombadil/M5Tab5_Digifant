#include "../src/measurement_model.h"

#include <cassert>

int main() {
  digifant::application::MeasurementModel model;
  digifant::transport::KwpFrameEnvelope frame{};
  frame.transportGeneration = 7;
  frame.sessionEpoch = 3;
  frame.rxSequence = 11;
  frame.completedUs = 1234;
  model.onProtocolStatus(true, true, 7, 3);
  assert(model.snapshot().k409Connected && model.snapshot().kwpConnected);
  assert(!model.snapshot().ecuDataValid);

  digifant::diagnostic::DiagnosticResult rpm{};
  rpm.valid = true;
  rpm.group = 0;
  rpm.group000Decoded = true;
  rpm.rpm = 950;
  assert(model.apply(frame, rpm));
  auto snapshot = model.snapshot();
  assert(snapshot.rpm == 0);
  assert(snapshot.ecuDataValid);
  assert(snapshot.fields[9].decodedValue == 950.0f);
  assert(snapshot.transportGeneration == 7 && snapshot.sessionEpoch == 3);
  assert(snapshot.lastRxSequence == 11 && snapshot.lastTimestampUs == 1234);

  digifant::diagnostic::DiagnosticResult battery{};
  battery.valid = true;
  battery.decoded = true;
  battery.group = 2;
  battery.valueCount = 1;
  battery.values[0] = {3, 0x85, 128, true, 12.0f};
  frame.rxSequence = 12;
  frame.completedUs = 1300;
  assert(model.apply(frame, battery));
  snapshot = model.snapshot();
  assert(snapshot.batteryRaw == 128 && snapshot.validity == digifant::ui::SignalValidity::Valid);

  digifant::diagnostic::DiagnosticResult referenceRpm{};
  referenceRpm.valid = true;
  referenceRpm.decoded = true;
  referenceRpm.group = 1;
  referenceRpm.valueCount = 1;
  referenceRpm.values[0] = {1, 0x8B, 0, true, 0.0f, 26};
  frame.rxSequence = 13;
  assert(model.apply(frame, referenceRpm));
  assert(model.snapshot().rpm == 0);

  assert(model.onRuntimeTelemetry(42, 2, 3, 4, 5, 6, 7));
  assert(model.snapshot().frameCount == 42 && model.snapshot().frameDrops == 2);
  assert(model.snapshot().rxIngressDrops == 3 && model.snapshot().parserRejects == 4);
  assert(model.snapshot().actionFailures == 5 && model.snapshot().byteFault == 6);
  assert(model.snapshot().snapshotOverwrites == 7);
  assert(!model.onRuntimeTelemetry(42, 2, 3, 4, 5, 6, 7));

  model.onSequenceGap();
  assert(model.snapshot().validity == digifant::ui::SignalValidity::Stale);
  model.onSessionLost();
  assert(model.snapshot().validity == digifant::ui::SignalValidity::Stale);
  assert(model.snapshot().k409Connected && !model.snapshot().kwpConnected &&
         !model.snapshot().ecuDataValid);
  model.onDisconnect();
  assert(model.snapshot().validity == digifant::ui::SignalValidity::Disconnected);
  assert(!model.snapshot().k409Connected && !model.snapshot().kwpConnected);
  return 0;
}
