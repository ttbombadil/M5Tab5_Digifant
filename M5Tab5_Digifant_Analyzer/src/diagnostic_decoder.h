#pragma once

#include <array>
#include <cstdint>

#include "digifant_decoder.h"
#include "validated_frame_queue.h"

namespace digifant::diagnostic {

struct DiagnosticValue {
  uint8_t zone = 0;
  uint8_t formula = 0;
  uint8_t raw = 0;
  bool supported = false;
  float value = 0.0f;
  uint8_t nwb = 0;
};

struct DiagnosticResult {
  bool valid = false;
  bool decoded = false;
  bool group000Decoded = false;
  bool headerStored = false;
  uint8_t group = 0;
  uint8_t title = 0;
  uint8_t counter = 0;
  uint8_t size = 0;
  uint8_t rawSize = 0;
  uint16_t rpm = 0;
  uint8_t group000ValueCount = 0;
  std::array<DiagnosticValue, 10> group000Values{};
  std::array<uint8_t, 65> raw{};
  uint8_t valueCount = 0;
  std::array<DiagnosticValue, 16> values{};
};

class DiagnosticDecoder {
 public:
  void resetSession() noexcept { header_valid_.fill(false); }

  DiagnosticResult process(const transport::KwpFrameEnvelope& frame) noexcept {
    DiagnosticResult result{};
    result.group = frame.groupHint;
    result.size = frame.size;
    result.rawSize = frame.size <= result.raw.size() ? frame.size : 0;
    for (uint8_t i = 0; i < result.rawSize; ++i) result.raw[i] = frame.bytes[i];

    const auto parsed = domain::parseKwpFrame(frame.bytes.data(), frame.size);
    if (!parsed.valid) {
      ++parser_rejected_;
      return result;
    }
    result.valid = true;
    result.title = frame.size > 2 ? frame.bytes[2] : 0;
    result.counter = parsed.counter;

    if (parsed.title == domain::ParsedTitle::GroupHeader && result.group >= 1 && result.group <= 4) {
      headers_[result.group] = parsed;
      header_generation_[result.group] = frame.transportGeneration;
      header_epoch_[result.group] = frame.sessionEpoch;
      header_valid_[result.group] = true;
      result.headerStored = true;
      return result;
    }

    if (parsed.title == domain::ParsedTitle::GroupBody && result.group == 0) {
      domain::DecodedGroup000 group000{};
      if (domain::decodeGroup000(parsed.payload.data(), parsed.payloadSize, group000)) {
        result.group000Decoded = true;
        result.rpm = group000.rpmEstimate;
        result.group000ValueCount = 10;
        const uint8_t* raw = &parsed.payload[0];
        for (uint8_t i = 0; i < 10; ++i)
          result.group000Values[i] = DiagnosticValue{i, 0, raw[i], false, 0.0f, 0};
      }
      return result;
    }

    if (parsed.title == domain::ParsedTitle::GroupBody && result.group >= 1 && result.group <= 4 &&
        header_valid_[result.group] && header_generation_[result.group] == frame.transportGeneration &&
        header_epoch_[result.group] == frame.sessionEpoch) {
      domain::DecodedNumberedGroup numbered{};
      if (domain::decodeNumberedGroup(result.group, headers_[result.group], parsed, numbered)) {
        result.decoded = true;
        result.valueCount = numbered.zoneCount;
        for (uint8_t i = 0; i < numbered.zoneCount; ++i) {
          result.values[i] = DiagnosticValue{numbered.zones[i].zone, numbered.zones[i].formula,
                                              numbered.zones[i].raw, numbered.zones[i].supported,
                                              numbered.zones[i].value, numbered.zones[i].nwb};
        }
      }
    }
    return result;
  }

  uint32_t parserRejected() const noexcept { return parser_rejected_; }

 private:
  std::array<domain::ParsedFrame, 5> headers_{};
  std::array<uint32_t, 5> header_generation_{};
  std::array<uint32_t, 5> header_epoch_{};
  std::array<bool, 5> header_valid_{};
  uint32_t parser_rejected_ = 0;
};

}  // namespace digifant::diagnostic
