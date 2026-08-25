#include "../src/sprotz_logger.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>

namespace {

uint32_t get32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8U) |
         (static_cast<uint32_t>(value[2]) << 16U) | (static_cast<uint32_t>(value[3]) << 24U);
}

uint64_t get64(const uint8_t* value) {
  uint64_t result = 0;
  for (uint8_t i = 0; i < 8; ++i) result |= static_cast<uint64_t>(value[i]) << (8U * i);
  return result;
}

class FakeSink final : public digifant::logging::LogSink {
 public:
  bool open(uint64_t, uint32_t, uint32_t) noexcept override {
    open_ = openWorks;
    size = 0;
    return open_;
  }
  bool write(const uint8_t* data, std::size_t count) noexcept override {
    if (!open_ || failWrite || count > bytes.size() - size) return false;
    std::memcpy(bytes.data() + size, data, count);
    size += count;
    return true;
  }
  bool flush() noexcept override { return !failFlush; }
  void close() noexcept override { open_ = false; }
  uint64_t freeBytes() const noexcept override {
    return forcedFree < bytes.size() ? forcedFree : bytes.size() - size;
  }
  const char* fileName() const noexcept override { return "/sprotz/test.dlog"; }

  std::array<uint8_t, 128 * 1024> bytes{};
  std::size_t size = 0;
  uint64_t forcedFree = bytes.size();
  bool openWorks = true;
  bool failWrite = false;
  bool failFlush = false;
  bool open_ = false;
};

digifant::ui::MeasurementSnapshot makeSnapshot(uint32_t sequence) {
  digifant::ui::MeasurementSnapshot snapshot{};
  snapshot.lastTimestampUs = 1000000ULL + sequence;
  snapshot.lastRxSequence = sequence;
  snapshot.sessionEpoch = 7;
  snapshot.transportGeneration = 3;
  snapshot.frameCount = sequence + 10;
  snapshot.rpm = 1234;
  snapshot.k409Connected = true;
  snapshot.kwpConnected = true;
  snapshot.ecuDataValid = true;
  snapshot.validity = digifant::ui::SignalValidity::Valid;
  for (uint8_t i = 0; i < snapshot.fieldCount; ++i) {
    auto& field = snapshot.fields[i];
    field.group = i < 10 ? 0 : static_cast<uint8_t>(1U + (i - 10U) / 4U);
    field.zone = i < 10 ? i : static_cast<uint8_t>(1U + (i - 10U) % 4U);
    field.raw = static_cast<uint8_t>(i + sequence);
    field.decodedValue = static_cast<float>(i) + 0.25f;
    field.timestampUs = snapshot.lastTimestampUs;
    field.rxSequence = sequence;
    field.sessionEpoch = 7;
    field.transportGeneration = 3;
  }
  return snapshot;
}

}  // namespace

int main() {
  using namespace digifant::logging;

  LoggerSnapshotQueue snapshots;
  for (uint32_t i = 0; i < LoggerSnapshotQueue::capacity(); ++i)
    assert(snapshots.trySend(makeSnapshot(i)));
  assert(!snapshots.trySend(makeSnapshot(99)));
  assert(snapshots.drops() == 1);
  assert(snapshots.highWatermark() == LoggerSnapshotQueue::capacity());
  for (uint32_t i = 0; i < LoggerSnapshotQueue::capacity(); ++i) {
    digifant::ui::MeasurementSnapshot value{};
    assert(snapshots.tryReceive(value));
    assert(value.lastRxSequence == i);
  }

  LoggerCommandQueue commands;
  assert(commands.trySend({LoggerCommandKind::Start, 100}));
  assert(commands.trySend({LoggerCommandKind::Marker, 200}));
  assert(commands.trySend({LoggerCommandKind::Stop, 300}));
  LoggerCommand command{};
  assert(commands.tryReceive(command) && command.kind == LoggerCommandKind::Start && command.timestampUs == 100);
  assert(commands.tryReceive(command) && command.kind == LoggerCommandKind::Marker && command.timestampUs == 200);
  assert(commands.tryReceive(command) && command.kind == LoggerCommandKind::Stop && command.timestampUs == 300);
  for (uint32_t i = 0; i < LoggerCommandQueue::capacity(); ++i)
    assert(commands.trySend({LoggerCommandKind::Marker, i}));
  assert(!commands.trySend({LoggerCommandKind::Marker, 99}));
  assert(commands.drops() == 1);
  while (commands.tryReceive(command)) {}

  LoggerCommandQueue mpscCommands;
  std::atomic<bool> producerOneDone{false};
  std::atomic<bool> producerTwoDone{false};
  std::atomic<uint32_t> acceptedCommands{0};
  constexpr uint32_t kCommandsPerProducer = 5000;
  std::thread producerOne([&] {
    for (uint32_t i = 0; i < kCommandsPerProducer; ++i) {
      if (mpscCommands.trySend({LoggerCommandKind::Marker, i}))
        acceptedCommands.fetch_add(1, std::memory_order_relaxed);
    }
    producerOneDone.store(true, std::memory_order_release);
  });
  std::thread producerTwo([&] {
    for (uint32_t i = 0; i < kCommandsPerProducer; ++i) {
      if (mpscCommands.trySend({LoggerCommandKind::Marker, i + kCommandsPerProducer}))
        acceptedCommands.fetch_add(1, std::memory_order_relaxed);
    }
    producerTwoDone.store(true, std::memory_order_release);
  });
  uint32_t receivedCommands = 0;
  while (!producerOneDone.load(std::memory_order_acquire) ||
         !producerTwoDone.load(std::memory_order_acquire)) {
    if (mpscCommands.tryReceive(command)) ++receivedCommands;
    else std::this_thread::yield();
  }
  while (mpscCommands.tryReceive(command)) ++receivedCommands;
  producerOne.join();
  producerTwo.join();
  assert(receivedCommands == acceptedCommands.load(std::memory_order_acquire));
  assert(receivedCommands + mpscCommands.drops() == 2U * kCommandsPerProducer);

  FakeSink sink;
  SprotzLoggerCore logger(sink);
  logger.storageReady(true);
  logger.acceptSnapshot(makeSnapshot(42));
  logger.handle({LoggerCommandKind::Start, 9000000});
  assert(logger.status().state == LoggerState::Recording);
  logger.acceptSnapshot(makeSnapshot(43));
  logger.handle({LoggerCommandKind::Marker, 9100000});
  logger.acceptSnapshot(makeSnapshot(44));
  logger.handle({LoggerCommandKind::Stop, 9200000});
  assert(logger.status().state == LoggerState::Ready);
  assert(logger.status().snapshotsWritten == 2);
  assert(logger.status().eventsWritten == 3);
  assert(logger.status().bytesWritten == BinaryLogFormat::kHeaderSize + 5 * BinaryLogFormat::kRecordSize);
  assert(std::strcmp(logger.status().fileName.data(), "/sprotz/test.dlog") == 0);

  assert(std::memcmp(sink.bytes.data(), "DGFTSPT1", 8) == 0);
  assert(get32(sink.bytes.data() + 12) == BinaryLogFormat::kRecordSize);
  assert(get64(sink.bytes.data() + 20) == 9000000);
  const uint8_t* start = sink.bytes.data() + BinaryLogFormat::kHeaderSize;
  assert(start[6] == static_cast<uint8_t>(LogRecordKind::Start));
  assert(get64(start + 8) == 9000000);
  assert(get32(start + 24) == 42);
  const uint8_t* sample = start + BinaryLogFormat::kRecordSize;
  assert(sample[6] == static_cast<uint8_t>(LogRecordKind::Snapshot));
  assert(get32(sample + 24) == 43);
  assert(sample[68] == 7);
  const uint8_t* firstField = sample + BinaryLogFormat::kRecordPrefixSize;
  assert(firstField[0] == 0 && firstField[1] == 0 && firstField[2] == 43);
  const uint8_t* lastField = firstField + 25 * BinaryLogFormat::kFieldSize;
  assert(lastField[0] == 4 && lastField[1] == 4 && lastField[2] == 68);
  assert(get32(lastField + 24) == 43);

  FakeSink fullSink;
  SprotzLoggerCore full(fullSink);
  full.storageReady(true);
  full.acceptSnapshot(makeSnapshot(1));
  full.handle({LoggerCommandKind::Start, 10});
  fullSink.forcedFree = BinaryLogFormat::kRecordSize - 1;
  full.acceptSnapshot(makeSnapshot(2));
  assert(full.status().state == LoggerState::StorageFull);
  assert(full.status().lastError == LoggerError::Full);

  FakeSink brokenSink;
  brokenSink.openWorks = false;
  SprotzLoggerCore broken(brokenSink);
  broken.storageReady(true);
  broken.handle({LoggerCommandKind::Start, 10});
  assert(broken.status().state == LoggerState::NoStorage);
  assert(broken.status().lastError == LoggerError::Open);

  FakeSink writeErrorSink;
  SprotzLoggerCore writeError(writeErrorSink);
  writeError.storageReady(true);
  writeError.handle({LoggerCommandKind::Start, 10});
  writeErrorSink.failWrite = true;
  writeError.acceptSnapshot(makeSnapshot(2));
  assert(writeError.status().state == LoggerState::WriteError);
  assert(writeError.status().lastError == LoggerError::Write);

  FakeSink flushErrorSink;
  SprotzLoggerCore flushError(flushErrorSink);
  flushError.storageReady(true);
  flushError.handle({LoggerCommandKind::Start, 10});
  flushErrorSink.failFlush = true;
  assert(!flushError.flush());
  assert(flushError.status().state == LoggerState::WriteError);

  FakeSink beforeEcuSink;
  SprotzLoggerCore beforeEcu(beforeEcuSink);
  beforeEcu.storageReady(true);
  beforeEcu.handle({LoggerCommandKind::Start, 10});
  const uint8_t* beforeEcuStart = beforeEcuSink.bytes.data() + BinaryLogFormat::kHeaderSize;
  const uint8_t* beforeEcuLastField = beforeEcuStart + BinaryLogFormat::kRecordPrefixSize +
                                      25 * BinaryLogFormat::kFieldSize;
  assert(beforeEcuLastField[0] == 4 && beforeEcuLastField[1] == 4);

  LoggerStatusMailbox mailbox;
  LoggerStatus status{};
  status.state = LoggerState::Recording;
  status.snapshotsWritten = 77;
  mailbox.publish(status);
  LoggerStatus received{};
  assert(mailbox.receive(received));
  assert(received.state == LoggerState::Recording && received.snapshotsWritten == 77);
  assert(!mailbox.receive(received));

  LoggerSnapshotQueue concurrentQueue;
  std::atomic<bool> producerDone{false};
  std::thread producer([&] {
    for (uint32_t sequence = 1; sequence <= 10000; ++sequence) {
      const auto value = makeSnapshot(sequence);
      while (!concurrentQueue.trySend(value)) std::this_thread::yield();
    }
    producerDone.store(true, std::memory_order_release);
  });
  uint32_t expected = 1;
  while (!producerDone.load(std::memory_order_acquire) || expected <= 10000) {
    digifant::ui::MeasurementSnapshot value{};
    if (!concurrentQueue.tryReceive(value)) {
      std::this_thread::yield();
      continue;
    }
    assert(value.lastRxSequence == expected++);
  }
  producer.join();
  assert(expected == 10001);

  LoggerStatusMailbox concurrentMailbox;
  std::atomic<bool> statusDone{false};
  std::thread statusProducer([&] {
    for (uint32_t publication = 1; publication <= 10000; ++publication) {
      LoggerStatus value{};
      value.snapshotsWritten = publication;
      value.eventsWritten = publication ^ 0x55AA55AAU;
      value.bytesWritten = static_cast<uint64_t>(publication) * 1112U;
      concurrentMailbox.publish(value);
    }
    statusDone.store(true, std::memory_order_release);
  });
  uint32_t lastPublication = 0;
  while (!statusDone.load(std::memory_order_acquire) || lastPublication < 10000) {
    LoggerStatus value{};
    if (!concurrentMailbox.receive(value)) {
      std::this_thread::yield();
      continue;
    }
    assert(value.eventsWritten == (value.snapshotsWritten ^ 0x55AA55AAU));
    assert(value.bytesWritten == static_cast<uint64_t>(value.snapshotsWritten) * 1112U);
    assert(value.snapshotsWritten >= lastPublication);
    lastPublication = value.snapshotsWritten;
  }
  statusProducer.join();
  assert(lastPublication == 10000);

  LoggerStatusFanout statusFanout;
  status.snapshotsWritten = 88;
  statusFanout.publish(status);
  assert(statusFanout.display().receive(received) && received.snapshotsWritten == 88);
  assert(statusFanout.serial().receive(received) && received.snapshotsWritten == 88);
  return 0;
}
