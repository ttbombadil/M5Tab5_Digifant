#pragma once

#include "measurement_snapshot_mailbox.h"

namespace digifant::ui {

struct UiState {
  MeasurementSnapshot snapshot{};
  bool hasSnapshot = false;
  bool connected = false;
};

inline void consumeSnapshot(LatestSnapshotMailbox& mailbox, UiState& state) noexcept {
  MeasurementSnapshot next{};
  if (!mailbox.receive(next)) return;
  state.snapshot = next;
  state.hasSnapshot = true;
  state.connected = next.validity != SignalValidity::Disconnected;
}

}  // namespace digifant::ui
