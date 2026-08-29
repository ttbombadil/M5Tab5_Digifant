#include <M5Unified.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ui_state.h"

// Touch-only baseline for isolating the intermittent Tab5 touch failure.
// Deliberately disabled: speaker, microphone, SD, I2C diagnostics, recovery.

namespace {

using audio_probe::TransitionResult;
using audio_probe::UiEvent;
using audio_probe::UiModel;
using audio_probe::UiState;

constexpr int16_t kFooterHeight = 54;
constexpr uint32_t kHeartbeatMs = 5000;
constexpr uint16_t kBackground = TFT_BLACK;
constexpr uint16_t kInactive = 0x2104;
constexpr uint16_t kDetail = 0x03EF;
constexpr uint16_t kCapturing = 0x0320;
constexpr uint16_t kConfirm = 0xB260;
constexpr uint16_t kResult = 0x4010;

const char* const kTestNames[audio_probe::kTestCount] = {
    "1/6 Pegel", "2/6 Rauschen", "3/6 Frequenz", "4/6 Kanal",
    "5/6 Dynamik", "6/6 Abschluss"};

UiModel model;
uint32_t physicalTouches = 0;
uint32_t serialEvents = 0;
uint32_t acceptedEvents = 0;
uint32_t rejectedEvents = 0;
uint32_t renders = 0;
uint32_t lastHeartbeat = 0;
char command[32] = {};
size_t commandLength = 0;

int16_t contentHeight() { return M5.Display.height() - kFooterHeight; }
int16_t rowHeight() { return contentHeight() / audio_probe::kTestCount; }

const char* eventName(UiEvent event) {
  switch (event) {
    case UiEvent::SelectTest: return "SELECT_TEST";
    case UiEvent::Activate: return "ACTIVATE";
    case UiEvent::Stop: return "STOP";
    case UiEvent::Resume: return "RESUME";
    case UiEvent::ConfirmStop: return "CONFIRM_STOP";
    case UiEvent::CompleteCapture: return "COMPLETE_CAPTURE";
    case UiEvent::Repeat: return "REPEAT";
    case UiEvent::Back: return "BACK";
    case UiEvent::None: return "NONE";
  }
  return "UNKNOWN";
}

uint16_t activeColor() {
  switch (model.state) {
    case UiState::Detail: return kDetail;
    case UiState::Capturing: return kCapturing;
    case UiState::StopConfirm: return kConfirm;
    case UiState::Result: return kResult;
    case UiState::List: return kInactive;
  }
  return kInactive;
}

void drawCentered(const char* text, int16_t left, int16_t top, int16_t width,
                  int16_t height, uint8_t size = 2) {
  M5.Display.setTextSize(size);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(text, left + width / 2, top + height / 2);
  M5.Display.setTextDatum(top_left);
}

void drawRow(uint8_t row) {
  const int16_t width = M5.Display.width();
  const int16_t height = rowHeight();
  const int16_t top = row * height;
  const bool active = model.state != UiState::List && row == model.selectedTest;
  const uint16_t background = active ? activeColor() : kInactive;

  M5.Display.fillRect(0, top, width, height - 2, background);
  M5.Display.setTextColor(TFT_WHITE, background);

  if (!active) {
    drawCentered(kTestNames[row], 0, top, width, height - 2, 3);
    return;
  }

  const int16_t zoneWidth = width / 3;
  M5.Display.drawFastVLine(zoneWidth, top, height - 2, TFT_LIGHTGREY);
  M5.Display.drawFastVLine(zoneWidth * 2, top, height - 2, TFT_LIGHTGREY);

  const char* left = "";
  const char* middle = "";
  const char* right = "";
  switch (model.state) {
    case UiState::Detail:
      left = "LISTE"; middle = "START SIM"; right = "START SIM"; break;
    case UiState::Capturing:
      left = middle = right = "STOP SIM"; break;
    case UiState::StopConfirm:
      left = "WEITER"; middle = right = "STOP OK"; break;
    case UiState::Result:
      left = "LISTE"; middle = "WIEDERHOLEN"; right = "LISTE"; break;
    case UiState::List:
      break;
  }
  drawCentered(left, 0, top, zoneWidth, height - 2);
  drawCentered(middle, zoneWidth, top, zoneWidth, height - 2);
  drawCentered(right, zoneWidth * 2, top, width - zoneWidth * 2, height - 2);
}

void drawFooter() {
  const int16_t top = contentHeight();
  const int16_t width = M5.Display.width();
  char line[160];
  std::snprintf(line, sizeof(line),
                "%s | Touch %lu | Serial %lu | OK %lu | Reject %lu",
                audio_probe::stateName(model.state),
                static_cast<unsigned long>(physicalTouches),
                static_cast<unsigned long>(serialEvents),
                static_cast<unsigned long>(acceptedEvents),
                static_cast<unsigned long>(rejectedEvents));
  M5.Display.fillRect(0, top, width, kFooterHeight, TFT_DARKGREY);
  M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREY);
  drawCentered(line, 0, top, width, kFooterHeight, 2);
}

void drawAll() {
  M5.Display.fillScreen(kBackground);
  for (uint8_t row = 0; row < audio_probe::kTestCount; ++row) drawRow(row);
  drawFooter();
  M5.Display.display();
  ++renders;
}

void drawTransition(const UiModel& before) {
  if (before.selectedTest != model.selectedTest) drawRow(before.selectedTest);
  drawRow(model.selectedTest);
  drawFooter();
  M5.Display.display();
  ++renders;
}

void printStatus(const char* reason) {
  Serial.printf(
      "STATUS reason=%s state=%s selected=%u touch=%lu serial=%lu accepted=%lu "
      "rejected=%lu renders=%lu uptime_ms=%lu heap=%lu\n",
      reason, audio_probe::stateName(model.state), model.selectedTest + 1,
      static_cast<unsigned long>(physicalTouches),
      static_cast<unsigned long>(serialEvents),
      static_cast<unsigned long>(acceptedEvents),
      static_cast<unsigned long>(rejectedEvents),
      static_cast<unsigned long>(renders), static_cast<unsigned long>(millis()),
      static_cast<unsigned long>(ESP.getFreeHeap()));
}

void applyEvent(UiEvent event, uint8_t selectedTest, const char* source) {
  const UiModel before = model;
  const TransitionResult result = audio_probe::dispatch(model, event, selectedTest);
  if (result.accepted) ++acceptedEvents;
  else ++rejectedEvents;

  Serial.printf("EVENT source=%s event=%s accepted=%s from=%s to=%s selected=%u\n",
                source, eventName(event), result.accepted ? "yes" : "no",
                audio_probe::stateName(before.state),
                audio_probe::stateName(model.state), model.selectedTest + 1);

  if (result.changed) drawTransition(before);
  else {
    drawFooter();
    M5.Display.display();
    ++renders;
  }
  printStatus(source);
}

void pollTouch() {
  if (M5.Touch.getCount() == 0) return;
  const auto detail = M5.Touch.getDetail(0);
  if (!detail.wasPressed()) return;

  ++physicalTouches;
  const int16_t height = rowHeight();
  if (detail.x < 0 || detail.y < 0 || detail.x >= M5.Display.width() ||
      detail.y >= contentHeight()) {
    ++rejectedEvents;
    Serial.printf("TOUCH_OUTSIDE x=%d y=%d\n", detail.x, detail.y);
    drawFooter();
    M5.Display.display();
    return;
  }

  const uint8_t row = static_cast<uint8_t>(detail.y / height);
  uint8_t zone = static_cast<uint8_t>((detail.x * 3) / M5.Display.width());
  if (zone > 2) zone = 2;
  const UiEvent event = audio_probe::eventForTap(model, row, zone);
  Serial.printf("TOUCH_PRESS number=%lu x=%d y=%d row=%u zone=%u\n",
                static_cast<unsigned long>(physicalTouches), detail.x, detail.y,
                row + 1, zone);
  applyEvent(event, row, "touch");
}

void printCommands() {
  Serial.println("COMMANDS: STATUS | LIST | NEXT | SELECT 1..6");
}

void executeCommand(char* value) {
  while (*value == ' ') ++value;
  for (char* p = value; *p; ++p) {
    if (*p >= 'a' && *p <= 'z') *p -= ('a' - 'A');
  }

  if (std::strcmp(value, "STATUS") == 0) {
    printStatus("serial");
    return;
  }
  if (std::strcmp(value, "LIST") == 0) {
    ++serialEvents;
    applyEvent(UiEvent::Back, model.selectedTest, "serial");
    return;
  }
  if (std::strcmp(value, "NEXT") == 0) {
    ++serialEvents;
    UiEvent event = UiEvent::None;
    switch (model.state) {
      case UiState::List: event = UiEvent::SelectTest; break;
      case UiState::Detail: event = UiEvent::Activate; break;
      case UiState::Capturing: event = UiEvent::Stop; break;
      case UiState::StopConfirm: event = UiEvent::ConfirmStop; break;
      case UiState::Result: event = UiEvent::Repeat; break;
    }
    applyEvent(event, model.selectedTest, "serial");
    return;
  }
  if (std::strncmp(value, "SELECT ", 7) == 0) {
    const int number = std::atoi(value + 7);
    ++serialEvents;
    applyEvent(UiEvent::SelectTest,
               number >= 1 && number <= audio_probe::kTestCount
                   ? static_cast<uint8_t>(number - 1)
                   : audio_probe::kTestCount,
               "serial");
    return;
  }
  printCommands();
}

void pollSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      command[commandLength] = '\0';
      if (commandLength) executeCommand(command);
      commandLength = 0;
    } else if (commandLength + 1 < sizeof(command)) {
      command[commandLength++] = c;
    }
  }
}

void heartbeat() {
  const uint32_t now = millis();
  if (now - lastHeartbeat < kHeartbeatMs) return;
  lastHeartbeat = now;
  printStatus("heartbeat");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  auto config = M5.config();
  config.clear_display = true;
  config.internal_imu = false;
  M5.begin(config);
  M5.Touch.begin(M5.Display.touch() ? &M5.Display : nullptr);

  drawAll();
  Serial.println("AUDIO_PROBE mode=TOUCH_ONLY_BASELINE");
  Serial.println("DISABLED speaker microphone sd i2c_diagnostics touch_recovery");
  Serial.printf("TOUCH_READY enabled=%s driver=%s display=%dx%d rotation=%u\n",
                M5.Touch.isEnabled() ? "yes" : "no",
                M5.Display.touch() ? "present" : "missing", M5.Display.width(),
                M5.Display.height(),
                static_cast<unsigned>(M5.Display.getRotation()));
  printCommands();
  printStatus("boot");
}

void loop() {
  M5.update();
  pollTouch();
  pollSerial();
  heartbeat();
  M5.delay(5);
}
