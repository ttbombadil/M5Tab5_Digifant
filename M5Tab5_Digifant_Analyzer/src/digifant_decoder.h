#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "kwp_application_parser.h"

namespace digifant::domain {

struct DecodedGroup000 {
  uint8_t iatRaw = 0, supplyVoltageRaw = 0, coolantRaw = 0, loadRaw = 0, lambdaVoltageRaw = 0;
  uint8_t lambdaTimerRaw = 0, probeStatusCounterRaw = 0, throttlePotVoltageRaw = 0;
  uint8_t injectionTimeRaw = 0, rpmRaw = 0;
  uint16_t rpmEstimate = 0;
};

inline bool decodeGroup000(const uint8_t* payload, uint8_t size, DecodedGroup000& out) noexcept {
  if (payload == nullptr || size != 10) return false;
  out = DecodedGroup000{payload[0], payload[1], payload[2], payload[3], payload[4],
                        payload[5], payload[6], payload[7], payload[8], payload[9],
                        static_cast<uint16_t>(payload[9] == 0 ? 0 : 163840U / payload[9])};
  return true;
}

inline bool decodeFormula(uint8_t formula, uint8_t nwb, const uint8_t* table,
                          uint8_t tableLen, uint8_t raw, float& value) noexcept {
  if (formula == 0x8B || formula == 0x8C) {
    if (table == nullptr || tableLen != 17) return false;
    // Real Digifant engine-off captures use the maximum 0x8B raw value as
    // the zero-speed endpoint. Without this endpoint handling the generic
    // 15/16 interpolation reports a physically false 29.25 rpm.
    if (formula == 0x8B && raw == 0xFF) {
      value = 0.0f;
      return true;
    }
    uint8_t index = static_cast<uint8_t>(raw / 16); if (index > 15) index = 15;
    const float interpolated = table[index] +
      (static_cast<float>(table[index + 1]) - table[index]) * (raw % 16) / 16.0f;
    value = formula == 0x8B ? interpolated * nwb : interpolated - nwb;
    return true;
  }
  if (formula == 0x85) { value = static_cast<float>(nwb) * raw / 256.0f; return true; }
  if (formula == 0x88) { value = static_cast<float>(raw & nwb); return true; }
  if (formula == 0x89) { value = static_cast<float>(nwb) * raw * 0.01f; return true; }
  return false;
}

struct DecodedZone {
  uint8_t zone = 0;
  uint8_t formula = 0;
  uint8_t nwb = 0;
  uint8_t tableLength = 0;
  uint8_t raw = 0;
  bool supported = false;
  float value = 0.0f;
};

struct DecodedNumberedGroup {
  uint8_t group = 0;
  uint8_t zoneCount = 0;
  std::array<DecodedZone, 16> zones{};
};

inline bool decodeNumberedGroup(uint8_t group, const ParsedFrame& header,
                                const ParsedFrame& body,
                                DecodedNumberedGroup& out) noexcept {
  if (group < 1 || group > 4 || !header.valid || !body.valid ||
      header.title != ParsedTitle::GroupHeader || body.title != ParsedTitle::GroupBody)
    return false;

  DecodedNumberedGroup next{};
  next.group = group;
  std::size_t headerPos = 0;
  std::size_t bodyPos = 0;
  while (headerPos < header.payloadSize) {
    if (next.zoneCount >= next.zones.size() || headerPos + 3 > header.payloadSize ||
        bodyPos >= body.payloadSize)
      return false;

    DecodedZone& zone = next.zones[next.zoneCount];
    zone.zone = static_cast<uint8_t>(next.zoneCount + 1);
    zone.formula = header.payload[headerPos++];
    zone.nwb = header.payload[headerPos++];
    zone.tableLength = header.payload[headerPos++];
    if (headerPos + zone.tableLength > header.payloadSize) return false;

    zone.raw = body.payload[bodyPos++];
    const uint8_t* table = zone.tableLength == 0 ? nullptr : &header.payload[headerPos];
    zone.supported = decodeFormula(zone.formula, zone.nwb, table, zone.tableLength,
                                   zone.raw, zone.value);
    headerPos += zone.tableLength;
    ++next.zoneCount;
  }
  if (bodyPos != body.payloadSize || next.zoneCount == 0) return false;
  out = next;
  return true;
}

}  // namespace digifant::domain
