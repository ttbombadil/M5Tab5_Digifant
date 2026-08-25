#include "../src/diagnostic_decoder.h"

#include <cassert>
#include <cmath>

using namespace digifant::diagnostic;
using namespace digifant::transport;

static KwpFrameEnvelope make_record(const uint8_t* bytes, uint8_t size, uint8_t group) {
  KwpFrameEnvelope record{};
  record.size = size;
  record.groupHint = group;
  for (uint8_t i = 0; i < size; ++i) record.bytes[i] = bytes[i];
  record.transportGeneration = 7;
  record.ingressEpoch = 3;
  record.sessionEpoch = 11;
  return record;
}

int main() {
  DiagnosticDecoder decoder;

  const uint8_t group000[] = {
      0x0D, 0x08, 0xF4, 0x37, 0x92, 0x09, 0x3E, 0x89, 0x00, 0x00,
      0x25, 0x05, 0xCC, 0x03};
  const auto g000 = decoder.process(make_record(group000, sizeof(group000), 0));
  assert(g000.valid && g000.group == 0 && g000.title == 0xF4);
  assert(g000.rawSize == sizeof(group000) && g000.raw[3] == 0x37);
  assert(g000.group000Decoded && g000.rpm == 163840U / 0xCCU);
  assert(g000.group000ValueCount == 10 && g000.group000Values[3].raw == 0x3E &&
         g000.group000Values[9].raw == 0xCC);

  const uint8_t group2_header[] = {
      0x31, 0x12, 0x02, 0x8B, 0x1A, 0x11, 0xFA, 0xE1, 0xE0, 0xBC,
      0xAD, 0x96, 0x84, 0x75, 0x61, 0x53, 0x48, 0x39, 0x28, 0x1F,
      0x19, 0x12, 0x00, 0x89, 0x32, 0x00, 0x85, 0x18, 0x00, 0x8C,
      0x28, 0x11, 0xA0, 0x64, 0x50, 0x44, 0x3A, 0x32, 0x2C, 0x26,
      0x21, 0x1B, 0x16, 0x10, 0x0B, 0x04, 0x00, 0x00, 0x00, 0x03};
  const auto header = decoder.process(make_record(group2_header, sizeof(group2_header), 2));
  assert(header.valid && header.group == 2 && header.headerStored);

  const uint8_t group2_body[] = {0x07, 0x14, 0xF4, 0xCE, 0x05, 0x92, 0x37, 0x03};
  const auto battery = decoder.process(make_record(group2_body, sizeof(group2_body), 2));
  assert(battery.valid && battery.decoded && battery.valueCount == 4);
  assert(battery.values[2].supported && battery.values[2].raw == 0x92);
  assert(std::fabs(battery.values[2].value - 13.6875f) < 0.0001f);

  const uint8_t group3_header[] = {
      0x20, 0x18, 0x02, 0x8B, 0x1A, 0x11, 0xFA, 0xE1, 0xE0, 0xBC,
      0xAD, 0x96, 0x84, 0x75, 0x61, 0x53, 0x48, 0x39, 0x28, 0x1F,
      0x19, 0x12, 0x00, 0x81, 0x64, 0x00, 0x84, 0x02, 0x00,
      0x81, 0x64, 0x00, 0x03};
  assert(decoder.process(make_record(group3_header, sizeof(group3_header), 3)).headerStored);
  const uint8_t group3_body[] = {0x07, 0x1A, 0xF4, 0xCC, 0x3B, 0x00, 0x70, 0x03};
  const auto g69 = decoder.process(make_record(group3_body, sizeof(group3_body), 3));
  assert(g69.valid && g69.decoded && !g69.values[2].supported);
  assert(g69.values[2].raw == 0x00);

  KwpFrameEnvelope invalid{};
  invalid.size = 4;
  invalid.bytes[0] = 4;
  const auto rejected = decoder.process(invalid);
  assert(!rejected.valid && decoder.parserRejected() == 1);
  return 0;
}
