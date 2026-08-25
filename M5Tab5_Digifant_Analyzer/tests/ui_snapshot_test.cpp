#include "../src/ui_state.h"
#include <cassert>

using namespace digifant::ui;

int main() {
  LatestSnapshotMailbox mailbox;
  UiState state{};
  MeasurementSnapshot first{};
  first.sessionEpoch = 3;
  first.rpm = 950;
  first.validity = SignalValidity::Valid;
  mailbox.publish(first);
  consumeSnapshot(mailbox, state);
  assert(state.hasSnapshot && state.connected && state.snapshot.rpm == 950);

  MeasurementSnapshot stale = first;
  stale.rpm = 1000;
  stale.validity = SignalValidity::Stale;
  mailbox.publish(stale);
  MeasurementSnapshot disconnected = stale;
  disconnected.validity = SignalValidity::Disconnected;
  mailbox.publish(disconnected);
  consumeSnapshot(mailbox, state);
  assert(state.snapshot.validity == SignalValidity::Disconnected);
  assert(!state.connected && state.snapshot.rpm == 1000);
  assert(mailbox.overwrites() == 1); // latest mailbox intentionally coalesces one intermediate value
  return 0;
}
