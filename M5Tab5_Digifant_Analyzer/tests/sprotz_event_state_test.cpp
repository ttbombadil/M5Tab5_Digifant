#include "../src/sprotz_event_state.h"
#include "../src/sprotz_logger_core.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

class FakeSink final : public digifant::logging::LogSink {
 public:
  bool open(uint64_t, uint32_t, uint32_t) noexcept override {
    open_ = true;
    size = 0;
    return true;
  }
  bool write(const uint8_t* data, std::size_t count) noexcept override {
    if (!open_ || count > bytes.size() - size) return false;
    std::memcpy(bytes.data() + size, data, count);
    size += count;
    return true;
  }
  bool flush() noexcept override { return true; }
  void close() noexcept override { open_ = false; }
  uint64_t freeBytes() const noexcept override { return bytes.size() - size; }
  const char* fileName() const noexcept override { return "/sprotz/event.dlog"; }

  std::array<uint8_t, 64 * 1024> bytes{};
  std::size_t size = 0;

 private:
  bool open_ = false;
};

digifant::ui::MeasurementSnapshot snapshot() {
  digifant::ui::MeasurementSnapshot value{};
  value.lastTimestampUs = 50;
  value.sessionEpoch = 2;
  value.transportGeneration = 3;
  value.k409Connected = true;
  value.kwpConnected = true;
  value.ecuDataValid = true;
  return value;
}

uint8_t kindAt(const FakeSink& sink, std::size_t index) {
  return sink.bytes[digifant::logging::BinaryLogFormat::kHeaderSize +
                    index * digifant::logging::BinaryLogFormat::kRecordSize + 6];
}

uint64_t timestampAt(const FakeSink& sink, std::size_t index) {
  const auto offset = digifant::logging::BinaryLogFormat::kHeaderSize +
                      index * digifant::logging::BinaryLogFormat::kRecordSize + 8;
  uint64_t value = 0;
  for (uint8_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(sink.bytes[offset + i]) << (8U * i);
  return value;
}

uint16_t get16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) | (static_cast<uint16_t>(value[1]) << 8U);
}

uint32_t get32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) | (static_cast<uint32_t>(value[1]) << 8U) |
         (static_cast<uint32_t>(value[2]) << 16U) | (static_cast<uint32_t>(value[3]) << 24U);
}

std::size_t v2RecordOffset(const FakeSink& sink, std::size_t index) {
  std::size_t offset = digifant::logging::BinaryLogV2Format::kHeaderSize;
  for (std::size_t current = 0; current < index; ++current) {
    assert(offset + digifant::logging::BinaryLogV2Format::kRecordHeaderSize <= sink.size);
    assert(get16(sink.bytes.data() + offset + 8) ==
           digifant::logging::BinaryLogV2Format::kRecordHeaderSize);
    offset += digifant::logging::BinaryLogV2Format::kRecordHeaderSize +
              get32(sink.bytes.data() + offset + 10);
  }
  assert(offset + digifant::logging::BinaryLogV2Format::kRecordHeaderSize <= sink.size);
  return offset;
}

}  // namespace

int main() {
  using namespace digifant::logging;

  SprotzEventStateMachine machine;
  assert(machine.preview(LoggerCommandKind::SprotzStart, false).action == SprotzEventAction::Ignore);
  assert(machine.preview(LoggerCommandKind::LogStart, false).action == SprotzEventAction::LogStart);
  machine.commit(SprotzEventAction::LogStart);
  assert(machine.preview(LoggerCommandKind::SprotzStart, true).action == SprotzEventAction::EventStart);
  machine.commit(SprotzEventAction::EventStart);
  assert(machine.active());
  assert(machine.preview(LoggerCommandKind::SprotzStart, true).action == SprotzEventAction::Ignore);
  assert(machine.preview(LoggerCommandKind::SprotzStop, true).action == SprotzEventAction::EventStop);

  FakeSink sink;
  SprotzLoggerCore core(sink);
  core.storageReady(true);
  core.acceptSnapshot(snapshot());
  core.handle({LoggerCommandKind::LogStart, 100});
  core.handle({LoggerCommandKind::SprotzStart, 200});
  core.handle({LoggerCommandKind::SprotzStart, 250});  // deterministic ignore
  core.handle({LoggerCommandKind::Marker, 300});
  core.handle({LoggerCommandKind::SprotzStop, 400});
  core.handle({LoggerCommandKind::SprotzStop, 450});  // deterministic ignore
  core.handle({LoggerCommandKind::LogStop, 500});

  assert(!core.status().sprotzActive);
  assert(core.status().eventsWritten == 5);
  assert(kindAt(sink, 0) == static_cast<uint8_t>(LogRecordKind::Start));
  assert(kindAt(sink, 1) == static_cast<uint8_t>(LogRecordKind::Marker));
  assert(kindAt(sink, 2) == static_cast<uint8_t>(LogRecordKind::Marker));
  assert(kindAt(sink, 3) == static_cast<uint8_t>(LogRecordKind::Marker));
  assert(kindAt(sink, 4) == static_cast<uint8_t>(LogRecordKind::Stop));
  assert(timestampAt(sink, 0) == 100);
  assert(timestampAt(sink, 1) == 200);
  assert(timestampAt(sink, 2) == 300);
  assert(timestampAt(sink, 3) == 400);
  assert(timestampAt(sink, 4) == 500);

  // LOG_STOP deterministically closes an unfinished event before STOP.
  FakeSink autoCloseSink;
  SprotzLoggerCore autoClose(autoCloseSink);
  autoClose.storageReady(true);
  autoClose.acceptSnapshot(snapshot());
  autoClose.handle({LoggerCommandKind::LogStart, 1000});
  autoClose.handle({LoggerCommandKind::SprotzStart, 1100});
  autoClose.handle({LoggerCommandKind::LogStop, 1200});
  assert(!autoClose.status().sprotzActive);
  assert(kindAt(autoCloseSink, 0) == static_cast<uint8_t>(LogRecordKind::Start));
  assert(kindAt(autoCloseSink, 1) == static_cast<uint8_t>(LogRecordKind::Marker));
  assert(kindAt(autoCloseSink, 2) == static_cast<uint8_t>(LogRecordKind::Marker));
  assert(kindAt(autoCloseSink, 3) == static_cast<uint8_t>(LogRecordKind::Stop));
  assert(timestampAt(autoCloseSink, 2) == 1200);

  // V2 keeps kind=MARKER but writes a checksum-protected event subtype payload.
  FakeSink v2Sink;
  SprotzLoggerCore v2(v2Sink, true);
  v2.storageReady(true);
  v2.acceptSnapshot(snapshot());
  v2.handle({LoggerCommandKind::LogStart, 2000});
  v2.handle({LoggerCommandKind::SprotzStart, 2100});
  v2.handle({LoggerCommandKind::Marker, 2200});
  v2.handle({LoggerCommandKind::SprotzStop, 2300});
  v2.handle({LoggerCommandKind::LogStop, 2400});
  assert(v2.status().eventsWritten == 5);
  assert(std::memcmp(v2Sink.bytes.data(), "DGFTSPT2", 8) == 0);

  constexpr std::array<uint8_t, 3> expectedSubtypes{
      static_cast<uint8_t>(BinaryLogV2Format::EventSubtype::SprotzStart),
      static_cast<uint8_t>(BinaryLogV2Format::EventSubtype::Marker),
      static_cast<uint8_t>(BinaryLogV2Format::EventSubtype::SprotzStop)};
  for (std::size_t index = 0; index < expectedSubtypes.size(); ++index) {
    const std::size_t offset = v2RecordOffset(v2Sink, 2 + index);
    assert(v2Sink.bytes[offset + 6] == static_cast<uint8_t>(V2RecordKind::Marker));
    assert(get32(v2Sink.bytes.data() + offset + 10) == BinaryLogV2Format::kEventPayloadSize);
    const uint8_t* payload = v2Sink.bytes.data() + offset + BinaryLogV2Format::kRecordHeaderSize;
    assert(payload[0] == BinaryLogV2Format::kEventPayloadSchemaV1);
    assert(payload[1] == expectedSubtypes[index]);
  }
  return 0;
}
