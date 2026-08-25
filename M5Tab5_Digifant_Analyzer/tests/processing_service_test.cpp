#include "../src/processing_service.h"

#include <cassert>

int main() {
  digifant::transport::ValidatedFrameQueue frames;
  digifant::transport::RxIngressRing rxIngress;
  digifant::ui::SnapshotConsumerFanout snapshots;
  digifant::logging::LoggerSnapshotQueue loggerSnapshots;
  digifant::processing::ProcessingService service(frames, rxIngress, snapshots,
                                                   loggerSnapshots);

  const digifant::processing::RuntimeStatus status{
      true, true, 4, 9, 0, 0, 0};
  assert(!service.poll(status));

  digifant::ui::MeasurementSnapshot snapshot{};
  assert(snapshots.serial().receive(snapshot));
  assert(snapshot.k409Connected && snapshot.kwpConnected);

  digifant::transport::KwpFrameEnvelope frame{};
  frame.size = 4;
  frame.bytes[0] = 3;
  frame.bytes[1] = 0;
  frame.bytes[2] = 0xF6;
  frame.bytes[3] = 3;
  frame.transportGeneration = 4;
  frame.sessionEpoch = 9;
  frame.completedUs = 1234;
  assert(frames.trySend(frame));
  assert(service.poll(status));

  assert(loggerSnapshots.tryReceive(snapshot));
  assert(snapshot.kwpConnected && snapshot.lastRxSequence == 0);
  assert(snapshots.serial().receive(snapshot));
  assert(snapshot.lastRxSequence == 0);
  return 0;
}
