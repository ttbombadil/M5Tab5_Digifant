#include "../src/diagnostic_decoder.h"
#include "../src/kwp_measurement_session.h"

#include <cassert>

using namespace digifant::application;
using namespace digifant::transport;

int main() {
  ValidatedFrameQueue queue;
  KwpMeasurementSession session;
  session.setValidatedFrameSink(queue);
  session.start(4, 9, 50);

  const uint8_t frame[] = {3, 0, 0xF6, 0x03};
  for (uint8_t i = 0; i < sizeof(frame); ++i) {
    session.onRxByte(frame[i], 200 + i);
    digifant::kwp::KwpAction action{};
    while (session.popAction(action)) {
      session.onRxByte(static_cast<uint8_t>(action.value), 300 + i);
      assert(session.onCompletion(action.token, digifant::kwp::CompletionStatus::Completed));
    }
  }

  KwpFrameEnvelope envelope{};
  assert(queue.tryReceive(envelope));
  assert(envelope.transportGeneration == 4);
  assert(envelope.sessionEpoch == 9);
  assert(envelope.size == 4 && envelope.bytes[2] == 0xF6);

  digifant::diagnostic::DiagnosticDecoder decoder;
  const auto decoded = decoder.process(envelope);
  assert(decoded.valid && decoded.title == 0xF6);
  return 0;
}
