#include "../src/measurement_snapshot.h"

#include <cassert>

int main() {
  digifant::ui::MeasurementSnapshot snapshot{};
  snapshot.transportGeneration = 3;
  snapshot.sessionEpoch = 4;
  snapshot.lastRxSequence = 22;
  snapshot.rpm = 950;
  snapshot.batteryRaw = 128;
  snapshot.validity = digifant::ui::SignalValidity::Valid;

  digifant::ui::LatestSnapshotMailbox mailbox;
  mailbox.publish(snapshot);
  digifant::ui::MeasurementSnapshot received{};
  assert(mailbox.receive(received));
  assert(received.transportGeneration == 3 && received.sessionEpoch == 4);
  assert(received.lastRxSequence == 22 && received.rpm == 950);
  assert(received.batteryRaw == 128 && received.validity == digifant::ui::SignalValidity::Valid);
  return 0;
}
