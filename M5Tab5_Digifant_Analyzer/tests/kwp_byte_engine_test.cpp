#include "../src/kwp_byte_engine.h"
#include <cassert>

using namespace digifant::kwp;

int main() {
  KwpByteEngine engine;
  engine.reset();
  assert(engine.beginHostByte(0x12, true));
  assert(engine.takeTxRequest().byte == 0x12);
  engine.onRxByte(0x12);
  engine.completion(true);
  assert(engine.state() == ByteEngineState::HostAck);
  engine.onRxByte(static_cast<uint8_t>(~0x12));
  assert(engine.state() == ByteEngineState::Idle);

  engine.reset();
  engine.onRxByte(0x04);
  auto ack = engine.takeTxRequest();
  assert(ack.pending && ack.byte == static_cast<uint8_t>(~0x04));
  // The ECU may publish its next byte before the ACK echo. It is retained
  // and processed only after the echo/completion pair closes.
  engine.onRxByte(0x01);
  engine.onRxByte(ack.byte);
  engine.completion(true);
  assert(engine.state() == ByteEngineState::RxByteEcho);

  // counter, title, terminator follow
  ack = engine.takeTxRequest();
  assert(ack.pending && ack.byte == static_cast<uint8_t>(~0x01));
  engine.completion(true);
  engine.onRxByte(ack.byte);
  assert(engine.state() == ByteEngineState::RxByte);
  engine.onRxByte(0x09);
  ack = engine.takeTxRequest(); engine.completion(true); engine.onRxByte(ack.byte);
  engine.onRxByte(0x00);
  ack = engine.takeTxRequest(); engine.completion(true); engine.onRxByte(ack.byte);
  engine.onRxByte(0x03);
  assert(engine.frameComplete() && engine.frameSize() == 5);

  // A complete host ACK block can be followed by an ECU length byte before
  // the final ACK echo is consumed. The deferred byte must start a new RX
  // frame, not be interpreted while the engine is still Idle.
  engine.reset();
  assert(engine.beginHostByte(0x09, true));
  engine.onRxByte(0x09);
  engine.completion(true);
  engine.onRxByte(0x04);  // next ECU frame length arrives early
  engine.onRxByte(static_cast<uint8_t>(~0x09));
  assert(engine.state() == ByteEngineState::RxLengthEcho);
  ack = engine.takeTxRequest();
  assert(ack.pending && ack.byte == static_cast<uint8_t>(~0x04));

  engine.reset();
  assert(engine.beginHostByte(0x55, false));
  engine.onRxByte(0x54);
  assert(engine.state() == ByteEngineState::HostEcho);
  engine.onRxByte(0x53);
  assert(engine.state() == ByteEngineState::Fault && engine.fault() == ByteEngineFault::UnexpectedByte);

  engine.reset();
  assert(engine.beginHostByte(0x03, false));
  engine.onRxByte(0x04);  // next ECU byte before local echo
  engine.onRxByte(0x03);
  engine.completion(true);
  assert(engine.state() == ByteEngineState::RxLengthEcho);
  return 0;
}
