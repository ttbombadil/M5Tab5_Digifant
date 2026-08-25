#include "../src/kwp_byte_engine.h"

#include <cassert>
#include <cstddef>
#include <type_traits>

using namespace digifant::kwp;

int main() {
  static_assert(std::is_trivially_copyable_v<ByteEngineTraceEntry>);

  KwpByteEngine engine;
  engine.clearTrace();
  engine.setTraceContext(ByteEngineTraceContext{
      123456, 7, 11, 19, 23, 3, 1, 2, 4});
  engine.reset();

  // Deterministic reproduction of the terminal symptom: a byte that cannot
  // be a KWP block length is consumed while the engine expects RxLength.
  engine.onRxByte(0x02);
  assert(engine.fault() == ByteEngineFault::InvalidLength);
  assert(engine.state() == ByteEngineState::Fault);
  assert(engine.traceCount() >= 2);

  const ByteEngineTraceEntry& entry = engine.traceEntry(engine.traceCount() - 1);
  assert(entry.timestampUs == 123456);
  assert(entry.direction == ByteTraceDirection::Rx);
  assert(entry.byte == 0x02);
  assert(entry.stateBefore == ByteEngineState::RxLength);
  assert(entry.stateAfter == ByteEngineState::Fault);
  assert(entry.expectedFrameSize == 0);
  assert(entry.frameByteIndex == 0);
  assert(entry.sessionBlockCounter == 4);
  assert(entry.semanticTurnId == 19);
  assert(entry.transportOperationId == 23);
  assert(entry.sessionState == 3);
  assert(entry.planStage == 1);
  assert(entry.planGroup == 2);
  assert(entry.fault == ByteEngineFault::InvalidLength);
  assert(!entry.echoSeen && !entry.ackSeen && !entry.pendingRxAfterEcho);

  // The trace is a fixed overwrite ring and retains only its most recent
  // records in chronological order.
  for (std::size_t i = 0; i < KwpByteEngine::kTraceCapacity + 8; ++i) {
    engine.setTraceContext(ByteEngineTraceContext{
        static_cast<uint64_t>(200000 + i), 7, 11, i, i, 3, 1, 2, 4});
    engine.reset();
  }
  assert(engine.traceCount() == KwpByteEngine::kTraceCapacity);
  assert(engine.traceEntry(0).timestampUs == 200008);
  return 0;
}
