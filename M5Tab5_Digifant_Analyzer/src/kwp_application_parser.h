#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace digifant::domain {

enum class ParsedTitle : uint8_t { Identification, Ack, GroupHeader, GroupBody, Refused, Unknown };
struct ParsedFrame {
  bool valid = false;
  uint8_t size = 0;
  uint8_t counter = 0;
  ParsedTitle title = ParsedTitle::Unknown;
  std::array<uint8_t, 61> payload{};
  uint8_t payloadSize = 0;
};

inline ParsedFrame parseKwpFrame(const uint8_t* bytes, std::size_t length) noexcept {
  ParsedFrame out{};
  if (bytes == nullptr || length < 4 || length > 65 || bytes[0] + 1U != length || bytes[length - 1] != 0x03)
    return out;
  out.valid = true;
  out.size = static_cast<uint8_t>(length);
  out.counter = bytes[1];
  out.payloadSize = static_cast<uint8_t>(length - 4);
  for (uint8_t i = 0; i < out.payloadSize; ++i) out.payload[i] = bytes[3 + i];
  switch (bytes[2]) {
    case 0xF6: out.title = ParsedTitle::Identification; break;
    case 0x09: out.title = ParsedTitle::Ack; break;
    case 0x02: out.title = ParsedTitle::GroupHeader; break;
    case 0xF4: out.title = ParsedTitle::GroupBody; break;
    case 0x0A: out.title = ParsedTitle::Refused; break;
    default: out.title = ParsedTitle::Unknown; break;
  }
  return out;
}

}  // namespace digifant::domain
