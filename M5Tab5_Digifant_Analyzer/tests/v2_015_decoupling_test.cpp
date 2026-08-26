#include "../src/measurement_snapshot.h"
#include "../src/validated_frame_queue.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>

namespace {

digifant::ui::MeasurementSnapshot makeSnapshot(uint32_t sequence) {
  digifant::ui::MeasurementSnapshot snapshot{};
  snapshot.lastRxSequence = sequence;
  snapshot.sessionEpoch = sequence ^ 0x55AA55AAU;
  snapshot.transportGeneration = sequence + 7U;
  snapshot.rpm = static_cast<uint16_t>(sequence & 0xFFFFU);
  snapshot.fields[0].rxSequence = sequence;
  snapshot.fields[0].raw = static_cast<uint8_t>(sequence & 0xFFU);
  return snapshot;
}

void assertConsistent(const digifant::ui::MeasurementSnapshot& snapshot) {
  const uint32_t sequence = snapshot.lastRxSequence;
  assert(snapshot.sessionEpoch == (sequence ^ 0x55AA55AAU));
  assert(snapshot.transportGeneration == sequence + 7U);
  assert(snapshot.rpm == static_cast<uint16_t>(sequence & 0xFFFFU));
  assert(snapshot.fields[0].rxSequence == sequence);
  assert(snapshot.fields[0].raw == static_cast<uint8_t>(sequence & 0xFFU));
}

}  // namespace

int main() {
  using digifant::transport::KwpFrameEnvelope;
  using digifant::transport::ValidatedFrameQueue;
  using digifant::ui::MeasurementSnapshot;
  using digifant::ui::SnapshotConsumerFanout;

  // A decoder stalled for a simulated 60 seconds cannot backpressure the
  // producer: the only effect is the queue's documented drop-newest policy.
  ValidatedFrameQueue frames;
  KwpFrameEnvelope frame{};
  for (uint32_t millisecond = 0; millisecond < 60000U; ++millisecond) {
    frame.completedUs = static_cast<uint64_t>(millisecond) * 1000U;
    (void)frames.trySend(frame);
  }
  assert(frames.highWatermark() == 32U);
  assert(frames.drops() == 60000U - 32U);
  uint32_t received = 0;
  while (frames.tryReceive(frame)) ++received;
  assert(received == 32U);
  assert(frames.trySend(frame));
  assert(frames.tryReceive(frame));
  assert(frame.rxSequence == 60000U);  // The downstream gap stays visible.

  // Serial and Display continue independently while Bluetooth/Web are slow
  // or permanently stalled. Optional consumers are enabled explicitly for
  // this stress fixture; production fanouts leave them disabled.
  SnapshotConsumerFanout fanout(true);
  assert(fanout.optionalConsumersEnabled());
  MeasurementSnapshot snapshot{};
  uint32_t last_display_sequence = 0;
  for (uint32_t sequence = 1; sequence <= 10000U; ++sequence) {
    fanout.publish(makeSnapshot(sequence));
    assert(fanout.serial().receive(snapshot));
    assertConsistent(snapshot);
    if (sequence < 4000U || sequence > 4500U) {
      assert(fanout.display().receive(snapshot));
      assertConsistent(snapshot);
      last_display_sequence = snapshot.lastRxSequence;
    }
    if ((sequence % 127U) == 0U) {
      assert(fanout.bluetooth().receive(snapshot));
      assertConsistent(snapshot);
    }
  }
  assert(last_display_sequence == 10000U);
  assert(fanout.web().receive(snapshot));
  assert(snapshot.lastRxSequence == 10000U);
  assert(fanout.serial().publishDrops() == 0U);
  assert(fanout.display().publishDrops() == 0U);
  assert(fanout.bluetooth().publishDrops() == 0U);
  assert(fanout.web().publishDrops() == 0U);

  // A permanently blocked Serial consumer is just another unobserved latest
  // mailbox; Display keeps receiving every publication.
  SnapshotConsumerFanout blocked_serial_fanout(true);
  assert(blocked_serial_fanout.optionalConsumersEnabled());
  for (uint32_t sequence = 1; sequence <= 10000U; ++sequence) {
    blocked_serial_fanout.publish(makeSnapshot(sequence));
    assert(blocked_serial_fanout.display().receive(snapshot));
    assertConsistent(snapshot);
  }
  assert(blocked_serial_fanout.serial().receive(snapshot));
  assert(snapshot.lastRxSequence == 10000U);
  assert(blocked_serial_fanout.serial().publishDrops() == 0U);
  assert(blocked_serial_fanout.display().publishDrops() == 0U);

  // Concurrent overwrite/read stress: a reader may see an old or a new
  // snapshot, but never a torn combination of both.
  digifant::ui::LatestSnapshotMailbox mailbox;
  std::atomic<bool> writerDone{false};
  std::thread writer([&] {
    for (uint32_t sequence = 1; sequence <= 50000U; ++sequence)
      mailbox.publish(makeSnapshot(sequence));
    writerDone.store(true, std::memory_order_release);
  });
  uint32_t last = 0;
  while (!writerDone.load(std::memory_order_acquire) ||
         last < mailbox.publishedCount()) {
    if (!mailbox.receive(snapshot)) continue;
    assertConsistent(snapshot);
    assert(snapshot.lastRxSequence >= last);
    last = snapshot.lastRxSequence;
  }
  writer.join();
  assert(last == 50000U);
  assert(mailbox.publishDrops() == 0U);
  return 0;
}
