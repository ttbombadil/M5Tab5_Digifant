#pragma once

#include "imu_sample_ring.h"
#include "logger_types.h"
#include "measurement_snapshot_types.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace digifant::logging {

class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual bool open(uint64_t startedAtUs, uint32_t sessionEpoch,
                    uint32_t transportGeneration) noexcept = 0;
  virtual bool write(const uint8_t* data, std::size_t size) noexcept = 0;
  virtual bool flush() noexcept = 0;
  virtual void close() noexcept = 0;
  virtual uint64_t freeBytes() const noexcept = 0;
  virtual const char* fileName() const noexcept = 0;
};

class BinaryLogFormat {
 public:
  static constexpr std::size_t kHeaderSize = 32;
  static constexpr std::size_t kFieldSize = 40;
  static constexpr std::size_t kRecordPrefixSize = 72;
  static constexpr std::size_t kRecordSize =
      kRecordPrefixSize + ui::MeasurementSnapshot::kFieldCount * kFieldSize;
  using Header = std::array<uint8_t, kHeaderSize>;
  using Record = std::array<uint8_t, kRecordSize>;

  static Header header(uint64_t startedAtUs) noexcept {
    Header out{};
    constexpr char magic[8] = {'D', 'G', 'F', 'T', 'S', 'P', 'T', '1'};
    for (std::size_t i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(magic[i]);
    put16(out.data(), 8, 1);
    put16(out.data(), 10, static_cast<uint16_t>(kHeaderSize));
    put32(out.data(), 12, static_cast<uint32_t>(kRecordSize));
    put16(out.data(), 16, ui::MeasurementSnapshot::kFieldCount);
    put64(out.data(), 20, startedAtUs);
    return out;
  }

  static const Record& record(LogRecordKind kind, uint64_t eventTimestampUs,
                              const ui::MeasurementSnapshot& snapshot) noexcept {
    static Record out{};
    out.fill(0);
    put32(out.data(), 0, 0x31524344U);  // "DCR1" little endian.
    put16(out.data(), 4, 1);
    out[6] = static_cast<uint8_t>(kind);
    out[7] = ui::MeasurementSnapshot::kFieldCount;
    put64(out.data(), 8, eventTimestampUs);
    put64(out.data(), 16, snapshot.lastTimestampUs);
    put32(out.data(), 24, snapshot.lastRxSequence);
    put32(out.data(), 28, snapshot.sessionEpoch);
    put32(out.data(), 32, snapshot.transportGeneration);
    put32(out.data(), 36, snapshot.frameCount);
    put32(out.data(), 40, snapshot.frameDrops);
    put32(out.data(), 44, snapshot.rxIngressDrops);
    put32(out.data(), 48, snapshot.parserRejects);
    put32(out.data(), 52, snapshot.actionFailures);
    put32(out.data(), 56, snapshot.snapshotOverwrites);
    put32(out.data(), 60, snapshot.faultCount);
    put16(out.data(), 64, snapshot.rpm);
    out[66] = snapshot.byteFault;
    out[67] = static_cast<uint8_t>(snapshot.validity);
    out[68] = static_cast<uint8_t>((snapshot.k409Connected ? 1U : 0U) |
                                   (snapshot.kwpConnected ? 2U : 0U) |
                                   (snapshot.ecuDataValid ? 4U : 0U));
    for (std::size_t i = 0; i < ui::MeasurementSnapshot::kFieldCount; ++i)
      encodeField(out.data() + kRecordPrefixSize + i * kFieldSize, snapshot.fields[i]);
    return out;
  }

 private:
  static void encodeField(uint8_t* out, const ui::MeasurementField& field) noexcept {
    out[0] = field.group;
    out[1] = field.zone;
    out[2] = field.raw;
    out[3] = field.formula;
    out[4] = field.nwb;
    out[5] = static_cast<uint8_t>(field.unit);
    out[6] = static_cast<uint8_t>(field.semantic);
    out[7] = static_cast<uint8_t>(field.semanticEvidence);
    out[8] = static_cast<uint8_t>(field.formulaEvidence);
    out[9] = static_cast<uint8_t>(field.status);
    put32(out, 12, std::bit_cast<uint32_t>(field.decodedValue));
    put64(out, 16, field.timestampUs);
    put32(out, 24, field.rxSequence);
    put32(out, 28, field.sessionEpoch);
    put32(out, 32, field.transportGeneration);
  }

  static void put16(uint8_t* out, std::size_t at, uint16_t value) noexcept {
    out[at] = static_cast<uint8_t>(value);
    out[at + 1] = static_cast<uint8_t>(value >> 8U);
  }
  static void put32(uint8_t* out, std::size_t at, uint32_t value) noexcept {
    for (uint8_t i = 0; i < 4; ++i) out[at + i] = static_cast<uint8_t>(value >> (8U * i));
  }
  static void put64(uint8_t* out, std::size_t at, uint64_t value) noexcept {
    for (uint8_t i = 0; i < 8; ++i) out[at + i] = static_cast<uint8_t>(value >> (8U * i));
  }
};

class BinaryLogV2Format {
 public:
  static constexpr std::size_t kHeaderSize = 64;
  static constexpr std::size_t kRecordHeaderSize = 26;
  static constexpr std::size_t kMaxPayloadSize = BinaryLogFormat::kRecordSize;
  using Header = std::array<uint8_t, kHeaderSize>;

  static Header header(uint64_t startedAtUs) noexcept {
    Header out{}; constexpr char magic[8] = {'D','G','F','T','S','P','T','2'};
    for (std::size_t i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(magic[i]);
    put16(out.data(), 8, 2); put16(out.data(), 10, kHeaderSize); put32(out.data(), 12, 0);
    put16(out.data(), 16, 26); put64(out.data(), 20, startedAtUs);
    out[28] = 1; out[29] = 1; out[30] = 2; // native axes, mg, mdps
    return out;
  }

  static std::array<uint8_t, 40> imuPayload(const imu::ImuSample& sample) noexcept {
    std::array<uint8_t, 40> out{};
    put64(out.data(), 0, sample.timestampUs); put32(out.data(), 8, sample.sequence);
    put32(out.data(), 12, static_cast<uint32_t>(sample.accelXMg));
    put32(out.data(), 16, static_cast<uint32_t>(sample.accelYMg));
    put32(out.data(), 20, static_cast<uint32_t>(sample.accelZMg));
    put32(out.data(), 24, static_cast<uint32_t>(sample.gyroXMdps));
    put32(out.data(), 28, static_cast<uint32_t>(sample.gyroYMdps));
    put32(out.data(), 32, static_cast<uint32_t>(sample.gyroZMdps)); out[36] = sample.validity;
    return out;
  }

  static std::array<uint8_t, 16> orientationPayload() noexcept {
    std::array<uint8_t, 16> out{}; out[0] = 1; out[1] = 1; out[2] = 2; return out;
  }

  using Record = std::array<uint8_t, kRecordHeaderSize + kMaxPayloadSize>;
  static const Record& record(
      V2RecordKind kind, uint64_t timestampUs, const uint8_t* payload,
      std::size_t payloadSize, std::size_t& used) noexcept {
    static Record out{};
    out.fill(0);
    used = kRecordHeaderSize + payloadSize; put32(out.data(), 0, 0x32434552U);
    put16(out.data(), 4, 2); out[6] = static_cast<uint8_t>(kind); put16(out.data(), 8, kRecordHeaderSize);
    put32(out.data(), 10, static_cast<uint32_t>(payloadSize)); put64(out.data(), 14, timestampUs);
    put32(out.data(), 22, checksum(payload, payloadSize));
    for (std::size_t i = 0; i < payloadSize; ++i) out[kRecordHeaderSize + i] = payload[i];
    return out;
  }

 private:
  static uint32_t checksum(const uint8_t* data, std::size_t size) noexcept {
    uint32_t value = 2166136261U;
    for (std::size_t i = 0; i < size; ++i) value = (value ^ data[i]) * 16777619U;
    return value;
  }
  static void put16(uint8_t* out, std::size_t at, uint16_t value) noexcept {
    out[at] = static_cast<uint8_t>(value); out[at + 1] = static_cast<uint8_t>(value >> 8U);
  }
  static void put32(uint8_t* out, std::size_t at, uint32_t value) noexcept {
    for (uint8_t i = 0; i < 4; ++i) out[at + i] = static_cast<uint8_t>(value >> (8U * i));
  }
  static void put64(uint8_t* out, std::size_t at, uint64_t value) noexcept {
    for (uint8_t i = 0; i < 8; ++i) out[at + i] = static_cast<uint8_t>(value >> (8U * i));
  }
};


static_assert(BinaryLogFormat::kRecordSize == 1112U);

}  // namespace digifant::logging
