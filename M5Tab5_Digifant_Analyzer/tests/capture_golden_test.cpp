#include "../src/digifant_decoder.h"
#include "../src/kwp_application_parser.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace digifant::domain;

namespace {

std::vector<uint8_t> parseHexField(const std::string& field) {
  std::vector<uint8_t> bytes;
  std::istringstream tokens(field);
  std::string token;
  while (tokens >> token) {
    assert(token.size() == 2);
    unsigned value = 0;
    std::istringstream converted(token);
    converted >> std::hex >> value;
    assert(!converted.fail() && value <= 0xFFU);
    bytes.push_back(static_cast<uint8_t>(value));
  }
  return bytes;
}

}  // namespace

int main() {
  std::ifstream capture("M5Tab5_Digifant_Analyzer/captures/engine_running_corrected_replay.csv");
  assert(capture.good());

  std::string line;
  assert(std::getline(capture, line));
  if (!line.empty() && line.back() == '\r') line.pop_back();
  assert(line == "index,group,title,length,frame_hex,payload_hex");

  unsigned dataRows = 0;
  unsigned parsedRows = 0;
  unsigned groupHeaders = 0;
  unsigned groupBodies = 0;
  bool batteryGoldenSeen = false;
  bool g69GoldenSeen = false;
  while (std::getline(capture, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    ++dataRows;

    std::istringstream fields(line);
    std::string index;
    std::string group;
    std::string title;
    std::string length;
    std::string frameHex;
    std::string payloadHex;
    assert(std::getline(fields, index, ','));
    assert(std::getline(fields, group, ','));
    assert(std::getline(fields, title, ','));
    assert(std::getline(fields, length, ','));
    assert(std::getline(fields, frameHex, ','));
    // ACK rows have an intentionally empty final payload field.
    (void)std::getline(fields, payloadHex);
    (void)index;
    (void)group;
    (void)title;
    (void)length;
    (void)payloadHex;

    const std::vector<uint8_t> bytes = parseHexField(frameHex);
    assert(!bytes.empty() && bytes.size() <= 65);
    const ParsedFrame frame = parseKwpFrame(bytes.data(), bytes.size());
    assert(frame.valid);
    ++parsedRows;

    if (frame.title == ParsedTitle::GroupHeader) {
      ++groupHeaders;
      assert(frame.payloadSize > 0);
    } else if (frame.title == ParsedTitle::GroupBody) {
      ++groupBodies;
      // The recorded group-2 body is the first complete battery golden frame.
      if (frame.payloadSize == 4 && frame.payload[0] == 0xCE &&
          frame.payload[1] == 0x05 && frame.payload[2] == 0x92 &&
          frame.payload[3] == 0x37) {
        const uint8_t headerBytes[] = {
            0x31, 0x12, 0x02, 0x8B, 0x1A, 0x11, 0xFA, 0xE1, 0xE0, 0xBC,
            0xAD, 0x96, 0x84, 0x75, 0x61, 0x53, 0x48, 0x39, 0x28, 0x1F,
            0x19, 0x12, 0x00, 0x89, 0x32, 0x00, 0x85, 0x18, 0x00, 0x8C,
            0x28, 0x11, 0xA0, 0x64, 0x50, 0x44, 0x3A, 0x32, 0x2C, 0x26,
            0x21, 0x1B, 0x16, 0x10, 0x0B, 0x04, 0x00, 0x00, 0x00, 0x03};
        const ParsedFrame header = parseKwpFrame(headerBytes, sizeof(headerBytes));
        DecodedNumberedGroup decoded{};
        assert(decodeNumberedGroup(2, header, frame, decoded));
        assert(decoded.zones[2].supported);
        assert(std::fabs(decoded.zones[2].value - 13.6875f) < 0.0001f);
        batteryGoldenSeen = true;
      }
      // The recorded group-3 body must remain a raw/unsupported G69 signal.
      if (frame.payloadSize == 4 && frame.payload[0] == 0xCC &&
          frame.payload[1] == 0x3B && frame.payload[2] == 0x00 &&
          frame.payload[3] == 0x70) {
        g69GoldenSeen = true;
      }
    }
  }

  assert(dataRows == 108);
  assert(parsedRows == dataRows);
  assert(groupHeaders >= 4);
  assert(groupBodies >= 4);
  assert(batteryGoldenSeen);
  assert(g69GoldenSeen);
  return 0;
}
