#include "../src/kwp_measurement_session.h"
#include "../src/rx_ingress_ring.h"

#include <cassert>

using namespace digifant::application;
using namespace digifant::kwp;

static void feed_frame(KwpMeasurementSession& session, uint8_t title) {
  const uint8_t frame[] = {3, 0, title, 0x03};
  for (uint8_t i = 0; i < sizeof(frame); ++i) {
    const uint8_t byte = frame[i];
    session.onRxByte(byte, 200);
    if (i == sizeof(frame) - 1) return;
    KwpAction action{};
    if (session.popAction(action)) {
      session.onRxByte(static_cast<uint8_t>(action.value), 201);
      session.onCompletion(action.token, CompletionStatus::Completed);
    }
  }
}

int main() {
  KwpMeasurementSession session;
  session.start(4, 9, 50);

  // Identification -> 0x09 ACK block; stale completion is ignored.
  feed_frame(session, 0xF6);
  KwpAction action{};
  assert(session.popAction(action));
  auto stale = action.token;
  stale.transportGeneration = 3;
  assert(!session.onCompletion(stale, CompletionStatus::Completed));
  assert(session.staleCompletions() == 1);
  session.onRxByte(static_cast<uint8_t>(action.value), 210);
  session.onRxByte(static_cast<uint8_t>(~action.value), 211);
  assert(session.onCompletion(action.token, CompletionStatus::Completed));

  // A disconnect retires the session and prevents further actions.
  session.disconnect();
  assert(session.state() == MeasurementSessionState::Fault);
  assert(!session.popAction(action));

  // Replay the captured ECU identification and second-block prefix. This
  // preserves the observed early-next-byte ordering without a transport.
  const uint8_t captured[] = {
      0x0F, 0xF0, 0x01, 0xFE, 0xF6, 0x09, 0x30, 0xCF, 0x33, 0xCC, 0x37,
      0xC8, 0x39, 0xC6, 0x30, 0xCF, 0x36, 0xC9, 0x30, 0xCF, 0x32, 0xCD,
      0x34, 0xCB, 0x41, 0xBE, 0x47, 0xB8, 0x20, 0xDF, 0x03, 0x03, 0xFC,
      0x00, 0xFF, 0x09, 0xF6, 0x03, 0x1B, 0xE4, 0x01, 0xFE, 0xF6, 0x09,
      0x44, 0xBB, 0x49, 0xB6, 0x47, 0xB8, 0x49, 0xB6, 0x46, 0xB9, 0x41,
      0xBE, 0x4E, 0xB1, 0x54, 0xAB, 0x20, 0xDF, 0x31, 0xCE};
  KwpMeasurementSession replay;
  replay.start(4, 9, 1000);
  for (const uint8_t byte : captured) {
    assert(replay.onRxByte(byte, 300));
    while (replay.popAction(action))
      assert(replay.onCompletion(action.token, CompletionStatus::Completed));
  }
  assert(replay.state() == MeasurementSessionState::Running);
  assert(replay.byteEngineFault() == 0);
  assert(replay.parsedFrames() >= 1);
  assert(replay.identificationFrames() >= 1);
  assert(replay.parserRejected() == 0);

  // The runner publishes validated values to the single bounded FrameQueue;
  // no parallel capture path and no second RX-ring consumer are involved.
  digifant::transport::ValidatedFrameQueue capture;
  KwpMeasurementSession captured_session;
  captured_session.start(8, 12, 2000);
  captured_session.setValidatedFrameSink(capture);
  feed_frame(captured_session, 0xF6);
  digifant::transport::KwpFrameEnvelope captured_record{};
  assert(capture.tryReceive(captured_record));
  assert(captured_record.transportGeneration == 8);
  assert(captured_record.sessionEpoch == 12);
  assert(captured_record.size == 4 && captured_record.bytes[2] == 0xF6);

  // The production boundary is the single RX ingress ring.  A complete
  // frame must reach the session only as value records carrying the original
  // batch metadata; direct byte injection is intentionally not used here.
  digifant::transport::RxIngressRing ingress;
  digifant::transport::ValidatedFrameQueue ring_capture;
  KwpMeasurementSession ring_session;
  ring_session.setValidatedFrameSink(ring_capture);
  ring_session.start(21, 7, 3000);
  const uint8_t ring_frame[] = {3, 0, 0xF6, 0x03};
  digifant::transport::RxIngressItem item{};
  uint8_t consumed = 0;
  uint64_t next_sequence = 900;
  for (const uint8_t byte : ring_frame) {
    const uint64_t frame_sequence = next_sequence++;
    assert(ingress.publishBatch(&byte, 1, 123456, 21, frame_sequence));
    assert(ingress.tryPop(item));
    assert(item.transportGeneration == 21);
    assert(item.ingressEpoch == 1);
    assert(item.transportEventSequence == frame_sequence);
    assert(item.batchTimestampUs == 123456);
    (void)ring_session.onRxItem(item);
    ++consumed;
    while (ring_session.popAction(action)) {
      const uint8_t echo = static_cast<uint8_t>(action.value);
      assert(ingress.publishBatch(&echo, 1, 123457, 21, next_sequence++));
      assert(ingress.tryPop(item));
      assert(ring_session.onRxItem(item));
      assert(ring_session.onCompletion(action.token, CompletionStatus::Completed));
    }
  }
  assert(consumed == sizeof(ring_frame));
  assert(ring_capture.nextSequence() == 1);
  assert(ring_session.ingressEpoch() == 1);

  // A stale generation is rejected at the same public boundary and cannot
  // alter the active session state.
  const auto state_before_stale = ring_session.state();
  item.transportGeneration = 20;
  assert(!ring_session.onRxItem(item));
  assert(ring_session.staleRxItems() == 1);
  assert(ring_session.state() == state_before_stale);
  return 0;
}
