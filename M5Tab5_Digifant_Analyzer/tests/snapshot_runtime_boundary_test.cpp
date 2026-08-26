#include "../src/measurement_snapshot_mailbox.h"

#include <cassert>

using digifant::ui::MeasurementSnapshot;
using digifant::ui::SignalValidity;
using digifant::ui::SnapshotConsumerFanout;

int main() {
  // Product contract: Processing publishes one snapshot to the declared
  // production consumer fanout; optional stress consumers are disabled.
  SnapshotConsumerFanout fanout;
  assert(!fanout.optionalConsumersEnabled());
  MeasurementSnapshot first{};
  first.sessionEpoch = 3;
  first.transportGeneration = 4;
  first.rpm = 950;
  first.validity = SignalValidity::Valid;
  fanout.publish(first);

  MeasurementSnapshot display{};
  MeasurementSnapshot serial{};
  assert(fanout.display().receive(display));
  assert(fanout.serial().receive(serial));
  assert(display.rpm == 950 && serial.rpm == 950);

  MeasurementSnapshot latest = first;
  latest.rpm = 1050;
  latest.validity = SignalValidity::Stale;
  fanout.publish(latest);
  assert(fanout.display().receive(display));
  assert(display.rpm == 1050 && display.validity == SignalValidity::Stale);
  assert(!fanout.bluetooth().receive(display));
  assert(!fanout.web().receive(display));
  return 0;
}
