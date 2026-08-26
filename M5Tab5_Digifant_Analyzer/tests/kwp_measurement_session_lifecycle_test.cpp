#include "../src/kwp_measurement_session.h"

#include <cassert>

using digifant::application::KwpMeasurementSession;
using digifant::application::MeasurementSessionState;
using digifant::kwp::CompletionStatus;
using digifant::kwp::KwpAction;

static void feed_identification(KwpMeasurementSession& session) {
  const uint8_t frame[] = {3, 0, 0xF6, 0x03};
  for (std::size_t i = 0; i < sizeof(frame); ++i) {
    session.onRxByte(frame[i], 200);
    if (i + 1U == sizeof(frame)) continue;
    KwpAction action{};
    if (session.popAction(action)) {
      session.onRxByte(static_cast<uint8_t>(action.value), 201);
      assert(session.onCompletion(action.token, CompletionStatus::Completed));
    }
  }
}

int main() {
  // Product contract: operation completion is bound to the active
  // transport/session generation and operation token.
  KwpMeasurementSession session;
  session.start(4, 9, 50);
  assert(session.state() == MeasurementSessionState::Running);
  feed_identification(session);

  KwpAction action{};
  assert(session.popAction(action));
  auto stale = action.token;
  stale.transportGeneration = 3;
  assert(!session.onCompletion(stale, CompletionStatus::Completed));
  assert(session.staleCompletions() == 1);
  assert(session.onCompletion(action.token, CompletionStatus::Completed));

  session.disconnect();
  assert(session.state() == MeasurementSessionState::Fault);
  assert(!session.popAction(action));
  return 0;
}
