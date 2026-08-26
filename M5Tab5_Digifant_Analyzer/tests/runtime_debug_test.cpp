#include "../src/runtime_debug.h"

#include <cassert>
#include <cstdint>

int main() {
  digifant::runtime::RuntimeDebug debug;
  using digifant::runtime::DebugPhase;
  using digifant::runtime::DebugTask;

  debug.beginLoop(DebugTask::Display, DebugPhase::LoopBegin, 100, 300);
  debug.setPhase(DebugTask::Display, DebugPhase::DisplayBeforeDraw, 110);
  debug.observeLoopDuration(DebugTask::Display, 42);
  debug.beginLoop(DebugTask::Display, DebugPhase::LoopBegin, 120, 250);
  debug.observeLoopDuration(DebugTask::Display, 7);
  const auto& display = debug.task(DebugTask::Display);
  assert(display.heartbeat.load() == 2);
  assert(display.lastAliveUs.load() == 120);
  assert(display.phase.load() == static_cast<uint8_t>(DebugPhase::LoopBegin));
  assert(display.maxLoopUs.load() == 42);
  assert(display.stackHighWaterWords.load() == 250);

  const uint32_t request = debug.noteTabRequest(2, 200);
  debug.noteTabApplied(2, request, 210);
  debug.noteTabRendered(2, request, 220);
  assert(request == 1);
  assert(debug.requestedTab() == 2 && debug.requestedTabSequence() == request);
  assert(debug.appliedTab() == 2 && debug.appliedTabSequence() == request);
  assert(debug.renderedTab() == 2 && debug.renderedTabSequence() == request);

  debug.noteSerialPoll();
  debug.noteSerialRxBytes(9);
  debug.noteSerialCommand();
  debug.noteSerialResponseStarted();
  debug.noteSerialResponseCompleted();
  assert(debug.serialPolls() == 1);
  assert(debug.serialRxBytes() == 9);
  assert(debug.serialCommands() == 1);
  assert(debug.serialResponsesStarted() == 1);
  assert(debug.serialResponsesCompleted() == 1);

  debug.noteTouch(1, true, 123, 456, 230);
  assert(debug.touchSamples() == 1 && debug.touchPresses() == 1);
  assert(debug.touchCount() == 1 && debug.lastTouchX() == 123 && debug.lastTouchY() == 456);
  assert(debug.lastTouchUs() == 230);

  for (uint32_t i = 0; i < 300; ++i) debug.record(1'000 + i, DebugTask::Serial,
                                                    DebugPhase::SerialPoll, i);
  assert(debug.flightSequence() >= 300);
  const auto current = debug.flight(debug.flightSequence());
  assert(current.valid && current.task == DebugTask::Serial && current.value == 299);
  assert(!debug.flight(1).valid);
  return 0;
}
