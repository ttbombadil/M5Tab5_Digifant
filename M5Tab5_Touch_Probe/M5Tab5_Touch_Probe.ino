#include <M5Unified.h>

// Eigenstaendiger Touch-Test fuer den M5Stack Tab5.
// Jeder neue Touch wird optisch markiert, gezaehlt und akustisch bestaetigt.

namespace {

constexpr uint8_t kSpeakerVolume = 8;
constexpr uint16_t kTouchToneHz = 1500;
constexpr uint16_t kTouchToneMs = 70;
constexpr int16_t kStatusHeight = 150;

bool touchWasDown = false;
uint32_t touchCount = 0;
int16_t lastTouchX = -1;
int16_t lastTouchY = -1;
uint16_t markerColor = TFT_GREEN;

void showScreen() {
  const int16_t width = M5.Display.width();
  const int16_t height = M5.Display.height();

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.fillRect(0, 0, width, kStatusHeight, TFT_DARKGREY);

  M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREY);
  M5.Display.setTextSize(4);
  M5.Display.setCursor(28, 22);
  M5.Display.print("M5Tab5 TOUCH TEST");

  M5.Display.setTextSize(2);
  M5.Display.setCursor(30, 86);
  M5.Display.print("Beruehre eine beliebige Stelle auf dem Display");

  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(30, kStatusHeight + 28);
  M5.Display.print("TOUCHES: ");
  M5.Display.print(touchCount);

  M5.Display.setCursor(30, kStatusHeight + 72);
  M5.Display.print("LETZTE KOORDINATE: ");
  if (lastTouchX >= 0) {
    M5.Display.print(lastTouchX);
    M5.Display.print(", ");
    M5.Display.print(lastTouchY);
  } else {
    M5.Display.print("-,-");
  }

  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(30, height - 42);
  M5.Display.print("Optisch: Kreis   Akustisch: kurzer Ton   Serial: 115200 Baud");
  M5.Display.display();
}

void updateTouchStatus() {
  // Nur die veraenderlichen Werte aktualisieren. Ein Vollbild-Refresh auf dem
  // Tab5 ist fuer eine einfache Rueckmeldung deutlich teurer.
  M5.Display.fillRect(24, kStatusHeight + 18, 720, 112, TFT_BLACK);

  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(30, kStatusHeight + 28);
  M5.Display.print("TOUCHES: ");
  M5.Display.print(touchCount);

  M5.Display.setCursor(30, kStatusHeight + 72);
  M5.Display.print("LETZTE KOORDINATE: ");
  if (lastTouchX >= 0) {
    M5.Display.print(lastTouchX);
    M5.Display.print(", ");
    M5.Display.print(lastTouchY);
  } else {
    M5.Display.print("-,-");
  }
  M5.Display.display();
}

void drawTouchFeedback(int16_t x, int16_t y) {
  const int16_t width = M5.Display.width();
  const int16_t height = M5.Display.height();

  if (x < 0 || y < kStatusHeight || x >= width || y >= height) return;

  M5.Display.fillCircle(x, y, 28, markerColor);
  M5.Display.drawCircle(x, y, 42, TFT_WHITE);
  M5.Display.drawFastHLine(x - 56, y, 112, TFT_WHITE);
  M5.Display.drawFastVLine(x, y - 56, 112, TFT_WHITE);
  M5.Display.display();

  markerColor = markerColor == TFT_GREEN ? TFT_YELLOW : TFT_GREEN;
}

void acknowledgeTouch(int16_t x, int16_t y, uint16_t size) {
  ++touchCount;
  lastTouchX = x;
  lastTouchY = y;

  Serial.printf("TOUCH_EVENT number=%lu x=%d y=%d size=%u\n",
                static_cast<unsigned long>(touchCount), x, y,
                static_cast<unsigned>(size));

  // Den Ton vor dem Display-Refresh starten: Die akustische Bestaetigung
  // bleibt damit unmittelbar, auch wenn der Panel-Refresh noch laeuft.
  M5.Speaker.tone(kTouchToneHz, kTouchToneMs);
  drawTouchFeedback(x, y);
  updateTouchStatus();
}

void pollTouch() {
  const uint8_t count = static_cast<uint8_t>(M5.Touch.getCount());
  if (count == 0) {
    touchWasDown = false;
    return;
  }

  const auto detail = M5.Touch.getDetail(0);
  if (!detail.isPressed()) {
    touchWasDown = false;
    return;
  }

  // Nur der erste Zyklus eines Fingerdrucks erzeugt Rueckmeldung.
  if (touchWasDown) return;
  touchWasDown = true;
  acknowledgeTouch(static_cast<int16_t>(detail.x),
                   static_cast<int16_t>(detail.y),
                   static_cast<uint16_t>(detail.size));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  auto config = M5.config();
  config.clear_display = true;
  config.internal_imu = false;
  M5.begin(config);

  // Explizit an das erkannte Display binden. Das ist der entscheidende
  // Unterschied zu einem Test, der nur M5.begin() aufruft.
  M5.Touch.begin(M5.Display.touch() ? &M5.Display : nullptr);

  M5.Speaker.begin();
  M5.Speaker.setVolume(kSpeakerVolume);

  Serial.println("M5Tab5_Touch_Probe gestartet");
  Serial.printf("TOUCH_READY enabled=%s driver=%s display=%dx%d rotation=%u\n",
                M5.Touch.isEnabled() ? "yes" : "no",
                M5.Display.touch() ? "present" : "missing",
                M5.Display.width(), M5.Display.height(),
                static_cast<unsigned>(M5.Display.getRotation()));
  Serial.println("TOUCH_EVENT erscheint bei jedem neuen Fingerdruck.");

  showScreen();
  if (!M5.Touch.isEnabled() || M5.Display.touch() == nullptr) {
    M5.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(30, kStatusHeight + 140);
    M5.Display.print("TOUCH TREIBER NICHT BEREIT");
    M5.Display.display();
    Serial.println("TOUCH_ERROR driver_not_ready");
  }
}

void loop() {
  M5.update();
  pollTouch();
  M5.delay(5);
}
