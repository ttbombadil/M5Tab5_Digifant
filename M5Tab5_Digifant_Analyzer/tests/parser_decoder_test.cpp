#include "../src/digifant_decoder.h"
#include "../src/kwp_application_parser.h"
#include <cassert>

using namespace digifant::domain;

int main() {
  const uint8_t group000[] = {0x0D, 0x08, 0xF4, 0x37, 0x92, 0x09, 0x3E, 0x89, 0x00, 0x00, 0x25, 0x05, 0xCC, 0x03};
  const ParsedFrame parsed = parseKwpFrame(group000, sizeof(group000));
  assert(parsed.valid && parsed.title == ParsedTitle::GroupBody && parsed.payloadSize == 10);
  DecodedGroup000 g{};
  assert(decodeGroup000(parsed.payload.data(), parsed.payloadSize, g));
  assert(g.rpmEstimate == 163840U / 0xCCU);
  assert(g.iatRaw == 0x37 && g.supplyVoltageRaw == 0x92 && g.coolantRaw == 0x09);
  assert(g.loadRaw == 0x3E && g.rpmRaw == 0xCC && g.throttlePotVoltageRaw == 0x25);
  assert(g.injectionTimeRaw == 0x05);
  uint8_t table[17] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  float value = 0;
  assert(decodeFormula(0x85, 0x18, nullptr, 0, 0x92, value) && value > 13.6f && value < 13.8f);
  assert(decodeFormula(0x8B, 2, table, 17, 0x28, value) && value > 4.9f && value < 5.1f);
  assert(decodeFormula(0x8B, 26, table, 17, 0xFF, value) && value == 0.0f);
  const uint8_t bad[] = {0x03, 0, 0x09, 0x04};
  assert(!parseKwpFrame(bad, sizeof(bad)).valid);
  return 0;
}
