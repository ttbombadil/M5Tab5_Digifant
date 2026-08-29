#include <M5Unified.h>
#include <FS.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

static constexpr uint32_t kSampleRate = 16000;
static constexpr uint32_t kMinDurationSeconds = 20;
static constexpr uint32_t kMaxDurationSeconds = 30;
static constexpr uint32_t kMaxFrames = kSampleRate * kMaxDurationSeconds;
static constexpr uint32_t kChunkFrames = kSampleRate / 4;  // 250 ms
static constexpr uint32_t kMaxSamples = kMaxFrames * 2;
static constexpr int16_t kNearFullScale = 32752;
static constexpr uint8_t kTestCount = 6;

static_assert(kMinDurationSeconds < kMaxDurationSeconds, "stop window must be usable");

static const char* const kTestTitles[kTestCount] = {
  "MOTOR AUS", "LEERLAUF", "1000 rpm", "1500 rpm", "2000 rpm", "2500 rpm"
};
static const char* const kTestShortAction[kTestCount] = {
  "Hintergrund aufnehmen", "Motor stabil im Leerlauf", "Drehzahl konstant halten",
  "Drehzahl konstant halten", "Drehzahl konstant halten", "Drehzahl konstant halten"
};
static const char* const kBeforeLine1[kTestCount] = {
  "Motor aus; Tueren/Fenster schliessen.", "Motor starten; Leerlauf abwarten.",
  "Etwa 1000 rpm einstellen.", "Etwa 1500 rpm einstellen.",
  "Etwa 2000 rpm einstellen.", "Etwa 2500 rpm einstellen."
};
static const char* const kBeforeLine2[kTestCount] = {
  "Messkonfiguration pruefen.", "Erst starten, wenn stabil.",
  "Erst starten, wenn stabil.", "Erst starten, wenn stabil.",
  "Erst starten, wenn stabil.", "Erst starten, wenn stabil."
};

enum class View : uint8_t { List, Detail, Capturing, Result, Error, AbortConfirm, StopConfirm };
enum class TestStatus : uint8_t { Open, Running, Done, Warning, Failed };
enum class TouchAction : uint8_t { None, Select, Left, Middle, Right };

// UI-Grundentscheidung: es gibt EINE Liste mit sechs Zellen. Die Zelle des
// gewaehlten Tests klappt in derselben Liste auf und zeigt darin Anweisung,
// Fortschritt/Zeit, Ergebnis, Zustand sowie die drei Aktionsbuttons. Alle
// anderen Zellen bleiben kompakt (Titel, Kurzauftrag, Status). Es gibt keine
// separate rechte Detailspalte mehr.
struct Rect { int16_t x; int16_t y; int16_t w; int16_t h; };
struct RowLayout { int16_t y; int16_t height; };

struct ChannelStats {
  int16_t min = INT16_MAX;
  int16_t max = INT16_MIN;
  int64_t sum = 0;
  uint64_t squares = 0;
  uint32_t nearFullScale = 0;
  uint32_t clippingEvents = 0;
  bool previousClipped = false;
};

struct TestResult {
  bool available = false;
  bool valid = false;
  bool warning = false;
  uint32_t frames = 0;
  uint64_t captureUs = 0;
  ChannelStats ch0;
  ChannelStats ch1;
};

static int16_t* g_pcm = nullptr;       // one 30 s stereo target buffer in PSRAM
static int16_t* g_chunk = nullptr;     // one 250 ms stereo chunk in PSRAM
static uint32_t g_validFrames = 0;
static uint32_t g_captureFrames = 0;
static uint32_t g_captureStartedMs = 0;
static uint64_t g_captureStartedUs = 0;
static uint64_t g_captureAudioUs = 0;
static uint32_t g_lastUiSecond = UINT32_MAX;
static uint8_t g_test = 0;
static int8_t g_lastCaptureTest = -1;
static bool g_stopRequested = false;
static bool g_wavWritten = false;
static View g_view = View::List;
static View g_abortReturnView = View::List;
static const char* g_feedback = "TEST AUSWAEHLEN";
static const char* g_error = "";
static TouchAction g_pressed = TouchAction::None;
static int8_t g_pressedTest = -1;
static bool g_touchDebugEnabled = true;
static uint32_t g_touchPollCount = 0;
static uint32_t g_touchHardwareEvents = 0;
static uint32_t g_touchAcceptedEvents = 0;
static uint32_t g_touchRejectedEvents = 0;
static uint32_t g_touchIntTransitions = 0;
static uint32_t g_touchLastHeartbeatMs = 0;
static int16_t g_touchLastX = -1;
static int16_t g_touchLastY = -1;
static int8_t g_touchLastIntLevel = -1;
static uint8_t g_touchLastUnifiedCount = 0;
static uint8_t g_touchLastDirectCount = 0;
static const char* g_touchLastSource = "none";
static char g_commandLine[40]{};
static uint8_t g_commandLength = 0;
static TestStatus g_status[kTestCount]{};
static TestResult g_results[kTestCount]{};

static void collectStats(ChannelStats& stats, int16_t value) {
  if (value < stats.min) stats.min = value;
  if (value > stats.max) stats.max = value;
  stats.sum += value;
  const int64_t wide = value;
  stats.squares += static_cast<uint64_t>(wide * wide);
  const bool clipped = value <= -kNearFullScale || value >= kNearFullScale;
  if (clipped) {
    ++stats.nearFullScale;
    if (!stats.previousClipped) ++stats.clippingEvents;
  }
  stats.previousClipped = clipped;
}

static uint16_t statusColor(TestStatus status) {
  switch (status) {
    case TestStatus::Running: return TFT_YELLOW;
    case TestStatus::Done: return TFT_GREEN;
    case TestStatus::Warning: return TFT_ORANGE;
    case TestStatus::Failed: return TFT_RED;
    case TestStatus::Open: return TFT_DARKGREY;
  }
  return TFT_WHITE;
}

static const char* statusText(TestStatus status) {
  switch (status) {
    case TestStatus::Running: return "LAEUFT";
    case TestStatus::Done: return "FERTIG";
    case TestStatus::Warning: return "WARNUNG";
    case TestStatus::Failed: return "FEHLER";
    case TestStatus::Open: return "OFFEN";
  }
  return "OFFEN";
}

static uint16_t viewColor() {
  if (g_view == View::Capturing) return TFT_YELLOW;
  if (g_view == View::Error || g_view == View::AbortConfirm) return TFT_RED;
  if (g_view == View::StopConfirm) return TFT_ORANGE;
  if (g_view == View::Result) return statusColor(g_status[g_test]);
  return TFT_CYAN;
}

static uint8_t completedCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < kTestCount; ++i) {
    if (g_status[i] == TestStatus::Done) ++count;
  }
  return count;
}

static bool resultHasProblem() {
  const TestResult& result = g_results[g_test];
  const double rate = result.captureUs ? 1000000.0 * result.frames / result.captureUs : 0.0;
  return !result.valid || result.warning || rate < kSampleRate * .98 || rate > kSampleRate * 1.02;
}

static bool actionEnabled(TouchAction action) {
  if (action == TouchAction::None || action == TouchAction::Select) return false;
  if (g_view == View::List) return false;
  if (g_view == View::Capturing) return action == TouchAction::Middle;
  if (g_view == View::StopConfirm) return action == TouchAction::Middle || action == TouchAction::Right;
  if (g_view == View::Result) {
    return action == TouchAction::Left || action == TouchAction::Middle || action == TouchAction::Right;
  }
  if (g_view == View::Error) return action == TouchAction::Left || action == TouchAction::Middle || action == TouchAction::Right;
  return true;
}

// ---------------------------------------------------------------------------
// Layout: EINE Liste, komplett statisch. Alle sechs Zeilen haben immer die
// gleiche Groesse und Position, unabhaengig davon, welcher Test gewaehlt ist.
// Eine Auswahl aendert nur den INHALT der Zeile (Anleitung, Fortschritt/Zeit,
// Ergebnis, Zustand, drei Aktionsbuttons); die Zeilenflaeche selbst wird nie
// vergroessert oder verkleinert. Es gibt keine separate zweite Spalte.
// ---------------------------------------------------------------------------

static int16_t outerMargin() { return 16; }
static int16_t listTopY() { return outerMargin(); }

static void computeRowLayout(RowLayout rows[kTestCount]) {
  const int16_t height = M5.Display.height();
  const int16_t top = listTopY();
  const int16_t available = height - top - outerMargin();
  const int16_t rowHeight = available / kTestCount;
  for (uint8_t i = 0; i < kTestCount; ++i) rows[i] = RowLayout{ static_cast<int16_t>(top + i * rowHeight), rowHeight };
}

static void computeButtonRects(const Rect& cell, Rect out[3]) {
  int16_t buttonHeight = cell.h * 22 / 100;
  if (buttonHeight < 48) buttonHeight = 48;
  if (buttonHeight > 96) buttonHeight = 96;
  const int16_t feedbackReserve = 26;
  const int16_t buttonY = cell.y + cell.h - buttonHeight - feedbackReserve;
  const int16_t gap = 16;
  const int16_t w = (cell.w - 2 * gap) / 3;
  out[0] = Rect{ cell.x, buttonY, w, buttonHeight };
  out[1] = Rect{ static_cast<int16_t>(cell.x + w + gap), buttonY, w, buttonHeight };
  out[2] = Rect{ static_cast<int16_t>(cell.x + 2 * (w + gap)), buttonY, w, buttonHeight };
}

static void drawButton(const Rect& r, const char* label, bool enabled, bool pressed) {
  const uint16_t fill = !enabled ? TFT_DARKGREY : pressed ? TFT_ORANGE : TFT_BLUE;
  M5.Display.fillRoundRect(r.x, r.y, r.w, r.h, 12, fill);
  M5.Display.drawRoundRect(r.x, r.y, r.w, r.h, 12, enabled ? TFT_WHITE : TFT_DARKGREY);
  M5.Display.setTextColor(enabled ? TFT_WHITE : TFT_LIGHTGREY, fill);
  M5.Display.setTextSize(3);

  const char* split = std::strchr(label, '\n');
  const int firstLength = split ? static_cast<int>(split - label) : static_cast<int>(std::strlen(label));
  char line[32]{};
  std::memcpy(line, label, firstLength);
  const int16_t lineHeight = 32;
  const int16_t firstY = r.y + r.h / 2 - (split ? lineHeight : 16);
  M5.Display.setCursor(r.x + (r.w - M5.Display.textWidth(line)) / 2, firstY);
  M5.Display.print(line);
  if (split) {
    std::strncpy(line, split + 1, sizeof(line) - 1);
    M5.Display.setCursor(r.x + (r.w - M5.Display.textWidth(line)) / 2, firstY + lineHeight);
    M5.Display.print(line);
  }
}

// Wortweiser Zeilenumbruch: bricht nur an Wortgrenzen um, nie mitten im Wort.
// Bricht spaetestens bei maxY ab, damit Text nie in die Buttons hineinlaeuft.
// Gibt die y-Position nach der letzten gezeichneten Zeile zurueck.
static int16_t drawWrapped(int16_t x, int16_t y, int16_t maxWidth, int16_t lineHeight, uint8_t textSize, const char* text, int16_t maxY) {
  M5.Display.setTextSize(textSize);
  char buffer[192];
  std::strncpy(buffer, text, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = 0;
  char* word = std::strtok(buffer, " ");
  char line[128] = {0};
  int16_t cursorY = y;
  while (word) {
    char candidate[128];
    if (line[0]) std::snprintf(candidate, sizeof(candidate), "%s %s", line, word);
    else std::snprintf(candidate, sizeof(candidate), "%s", word);
    if (M5.Display.textWidth(candidate) > maxWidth && line[0]) {
      if (cursorY + lineHeight <= maxY) { M5.Display.setCursor(x, cursorY); M5.Display.print(line); }
      cursorY += lineHeight;
      std::snprintf(line, sizeof(line), "%s", word);
    } else {
      std::snprintf(line, sizeof(line), "%s", candidate);
    }
    word = std::strtok(nullptr, " ");
  }
  if (line[0]) {
    if (cursorY + lineHeight <= maxY) { M5.Display.setCursor(x, cursorY); M5.Display.print(line); }
    cursorY += lineHeight;
  }
  return cursorY;
}

// Zeichnet eine einzelne Zeile nur, wenn sie noch vollstaendig oberhalb von
// bodyBottom liegt; verhindert Ueberlappung mit den Buttons in der Zelle.
static int16_t printClipped(int16_t x, int16_t y, int16_t lineHeight, int16_t bodyBottom, const char* text) {
  if (y + lineHeight <= bodyBottom) { M5.Display.setCursor(x, y); M5.Display.print(text); }
  return y + lineHeight;
}

static void drawCollapsedRow(uint8_t index, const Rect& cell) {
  const uint16_t background = g_pressedTest == static_cast<int8_t>(index) ? TFT_ORANGE : TFT_BLACK;
  M5.Display.fillRect(cell.x, cell.y, cell.w, cell.h, background);
  M5.Display.drawRect(cell.x, cell.y, cell.w, cell.h, TFT_DARKGREY);
  const int16_t x = cell.x + 12;
  M5.Display.setTextColor(TFT_WHITE, background);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(x, cell.y + cell.h * 6 / 100);
  M5.Display.printf("%u  %s", static_cast<unsigned>(index + 1), kTestTitles[index]);
  M5.Display.setTextColor(TFT_LIGHTGREY, background);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(x, cell.y + cell.h * 42 / 100);
  M5.Display.print(kTestShortAction[index]);
  M5.Display.setTextColor(statusColor(g_status[index]), background);
  M5.Display.setCursor(x, cell.y + cell.h * 68 / 100);
  M5.Display.printf("[ %s ]", statusText(g_status[index]));
  if (index == 0) {
    const char* label = g_touchLastX >= 0 ? "TOUCH ERKANNT" : "TOUCH-DIAG AKTIV";
    M5.Display.setTextColor(g_touchLastX >= 0 ? TFT_GREEN : TFT_CYAN, background);
    M5.Display.setTextSize(2);
    const int16_t labelWidth = M5.Display.textWidth(label);
    M5.Display.setCursor(cell.x + cell.w - labelWidth - 12, cell.y + 12);
    M5.Display.print(label);
  }
}

// Kopf der aufgeklappten Zelle: Nummer/Titel links, Zustandslabel rechts.
static int16_t drawExpandedHeader(const Rect& cell, const char* rightLabel) {
  const int16_t x = cell.x + 12;
  M5.Display.setTextColor(viewColor(), TFT_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(x, cell.y + 8);
  M5.Display.printf("%u/%u  %s", static_cast<unsigned>(g_test + 1), static_cast<unsigned>(kTestCount), kTestTitles[g_test]);
  M5.Display.setTextSize(2);
  const int16_t labelWidth = M5.Display.textWidth(rightLabel);
  M5.Display.setCursor(cell.x + cell.w - 12 - labelWidth, cell.y + 16);
  M5.Display.print(rightLabel);
  M5.Display.drawFastHLine(x, cell.y + 40, cell.w - 24, TFT_DARKGREY);
  return cell.y + 50;
}

static int16_t drawExpandedBody_Detail(const Rect& cell, int16_t bodyTop, int16_t bodyBottom) {
  const int16_t x = cell.x + 12;
  const int16_t w = cell.w - 24;
  int16_t y = bodyTop;
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  char combined[192];
  std::snprintf(combined, sizeof(combined), "%s %s", kBeforeLine1[g_test], kBeforeLine2[g_test]);
  y = drawWrapped(x, y, w, 22, 2, combined, bodyBottom);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  const TestResult& result = g_results[g_test];
  char resultLine[96];
  if (!result.available) {
    std::snprintf(resultLine, sizeof(resultLine), "Letztes Ergebnis: noch keines");
  } else {
    std::snprintf(resultLine, sizeof(resultLine), "Letztes Ergebnis: %.2f s | %.0f Hz",
      result.captureUs / 1000000.0,
      result.captureUs ? 1000000.0 * result.frames / result.captureUs : 0.0);
  }
  y = printClipped(x, y, 22, bodyBottom, resultLine);
  return y;
}

static int16_t drawExpandedBody_Capture(const Rect& cell, int16_t bodyTop, int16_t bodyBottom) {
  const int16_t x = cell.x + 12;
  const int16_t w = cell.w - 24;
  int16_t y = bodyTop;
  const float elapsed = static_cast<float>(g_captureFrames) / kSampleRate;
  const float shown = elapsed > kMaxDurationSeconds ? kMaxDurationSeconds : elapsed;
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  y = printClipped(x, y, 24, bodyBottom, kTestShortAction[g_test]);
  char timeLine[16];
  std::snprintf(timeLine, sizeof(timeLine), "%.1f s", elapsed);
  if (y + 30 <= bodyBottom) {
    M5.Display.setTextSize(3);
    M5.Display.setCursor(x + w / 2 - 40, y);
    M5.Display.print(timeLine);
  }
  y += 34;
  const int16_t barHeight = 20;
  if (y + barHeight <= bodyBottom) {
    M5.Display.fillRect(x, y, w, barHeight, TFT_DARKGREY);
    M5.Display.fillRect(x, y, static_cast<int32_t>(w * shown / kMaxDurationSeconds), barHeight, TFT_YELLOW);
  }
  y += barHeight + 6;
  M5.Display.setTextSize(2);
  y = printClipped(x, y, 22, bodyBottom, "Mindestdauer 20 s | Zielbereich 20 bis 30 s");
  return y;
}

static int16_t drawExpandedBody_Result(const Rect& cell, int16_t bodyTop, int16_t bodyBottom) {
  const int16_t x = cell.x + 12;
  const TestResult& result = g_results[g_test];
  const double rate = result.captureUs ? 1000000.0 * result.frames / result.captureUs : 0.0;
  const bool bad = resultHasProblem();
  int16_t y = bodyTop;
  M5.Display.setTextSize(2);
  char line[96];
  M5.Display.setTextColor(bad ? TFT_ORANGE : TFT_GREEN, TFT_BLACK);
  std::snprintf(line, sizeof(line), "Dauer %.2f s | Rate %.0f Hz", result.captureUs / 1000000.0, rate);
  y = printClipped(x, y, 22, bodyBottom, line);
  std::snprintf(line, sizeof(line), "CH0 Peak %d   CH1 Peak %d", result.ch0.max, result.ch1.max);
  y = printClipped(x, y, 22, bodyBottom, line);
  std::snprintf(line, sizeof(line), "Clipping %lu | Pegelreserve %s",
    static_cast<unsigned long>(result.ch0.clippingEvents + result.ch1.clippingEvents), bad ? "PRUEFEN" : "OK");
  y = printClipped(x, y, 22, bodyBottom, line);
  if (bad) { M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK); y = printClipped(x, y, 22, bodyBottom, "WIEDERHOLEN EMPFOHLEN"); }
  if (g_test == kTestCount - 1) {
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    y = printClipped(x, y, 22, bodyBottom, "Motor in Leerlauf, dann aus. WAV speichern?");
  }
  return y;
}

static int16_t drawExpandedBody_Message(const Rect& cell, int16_t bodyTop, int16_t bodyBottom, const char* line1, const char* line2) {
  const int16_t x = cell.x + 12;
  const int16_t w = cell.w - 24;
  int16_t y = bodyTop;
  M5.Display.setTextColor(viewColor(), TFT_BLACK);
  y = drawWrapped(x, y, w, 26, 2, line1, bodyBottom);
  if (line2 && line2[0]) y = drawWrapped(x, y, w, 26, 2, line2, bodyBottom);
  return y;
}

// Zeichnet die aufgeklappte Zelle des aktuell gewaehlten Tests: Kopf,
// zustandsabhaengiger Inhalt (Anleitung/Fortschritt/Ergebnis/Meldung),
// Buttons und Rueckmeldung -- alles innerhalb der Flaeche dieser einen Zelle.
static void drawExpandedCell(const Rect& cell) {
  const uint16_t background = g_pressedTest == static_cast<int8_t>(g_test) ? TFT_ORANGE : TFT_BLACK;
  M5.Display.fillRect(cell.x, cell.y, cell.w, cell.h, background);
  M5.Display.drawRect(cell.x, cell.y, cell.w, cell.h, viewColor());
  M5.Display.drawRect(cell.x + 1, cell.y + 1, cell.w - 2, cell.h - 2, viewColor());

  const char* rightLabel = "";
  if (g_view == View::Detail) rightLabel = statusText(g_status[g_test]);
  else if (g_view == View::Capturing) rightLabel = "AUFNAHME LAEUFT";
  else if (g_view == View::Result) rightLabel = resultHasProblem() ? "WARNUNG" : "FERTIG";
  else if (g_view == View::StopConfirm) rightLabel = "STOPP PRUEFEN";
  else if (g_view == View::AbortConfirm) rightLabel = "ABBRUCH";
  else rightLabel = "FEHLER";
  const int16_t bodyTop = drawExpandedHeader(cell, rightLabel);

  const int16_t bodyBottom = cell.y + cell.h - 34;

  if (g_view == View::Detail) drawExpandedBody_Detail(cell, bodyTop, bodyBottom);
  else if (g_view == View::Capturing) drawExpandedBody_Capture(cell, bodyTop, bodyBottom);
  else if (g_view == View::Result) drawExpandedBody_Result(cell, bodyTop, bodyBottom);
  else if (g_view == View::StopConfirm) drawExpandedBody_Message(cell, bodyTop, bodyBottom, "20 s sind noch nicht erreicht.", "Aufnahme wirklich stoppen?");
  else if (g_view == View::AbortConfirm) drawExpandedBody_Message(cell, bodyTop, bodyBottom, "Abbruch bestaetigen?", "Aufnahme/Testansicht verlassen?");
  else drawExpandedBody_Message(cell, bodyTop, bodyBottom, g_error, "");

  const char* nextAction = g_view == View::Detail ? "FELD ANTIPPEN: AUFNAHME START" :
                           g_view == View::Capturing ? "FELD ANTIPPEN: AUFNAHME STOP" :
                           g_view == View::StopConfirm ? "NOCH EINMAL: STOP BESTAETIGEN" :
                           g_view == View::Result ? "FELD ANTIPPEN: WIEDERHOLEN" :
                           g_view == View::Error ? "FELD ANTIPPEN: ERNEUT VERSUCHEN" :
                           "FELD ANTIPPEN: ABBRUCH BESTAETIGEN";
  M5.Display.setTextColor(viewColor(), background);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(cell.x + 12, cell.y + cell.h - 22);
  M5.Display.print(nextAction);
}

// Die eine Liste: sechs Zeilen, die gewaehlte Zeile ist aufgeklappt.
static void drawList() {
  RowLayout rows[kTestCount];
  computeRowLayout(rows);
  const int16_t x = outerMargin();
  const int16_t w = M5.Display.width() - 2 * outerMargin();
  for (uint8_t i = 0; i < kTestCount; ++i) {
    const Rect cell{ x, rows[i].y, w, rows[i].height - 6 };
    if (g_view != View::List && i == g_test) drawExpandedCell(cell);
    else drawCollapsedRow(i, cell);
  }
}

static void touchPoll();
static bool restoreTouchAfterFullRefresh(const char* reason);

static void drawScreen() {
  M5.Display.clear(TFT_BLACK);
  drawList();
  M5.Display.display();
  if (g_view != View::Capturing) restoreTouchAfterFullRefresh("full_refresh");
}

static TouchAction hitTestTouch(int16_t touchX, int16_t touchY, uint8_t& selectedTest) {
  selectedTest = 0;
  RowLayout rows[kTestCount];
  computeRowLayout(rows);
  const int16_t x = outerMargin();
  const int16_t w = M5.Display.width() - 2 * outerMargin();

  for (uint8_t i = 0; i < kTestCount; ++i) {
    const Rect cell{ x, rows[i].y, w, rows[i].height - 6 };
    if (touchX < cell.x || touchX >= cell.x + cell.w || touchY < cell.y || touchY >= cell.y + cell.h) continue;
    selectedTest = i;
    return TouchAction::Select;
  }
  return TouchAction::None;
}

static bool probeI2cAddress(int port, uint8_t address, uint32_t frequency = 100000) {
  return m5gfx::i2c::beginTransaction(port, address, frequency, false).has_value()
      && m5gfx::i2c::endTransaction(port).has_value();
}

static bool touchControllerResponds() {
  const int port = static_cast<int>(M5.In_I2C.getPort());
  return probeI2cAddress(port, 0x55) || probeI2cAddress(port, 0x14) || probeI2cAddress(port, 0x5D);
}

static bool recoverTouchController(const char* reason) {
  constexpr uint8_t ioExpanderAddress = 0x43;
  constexpr uint8_t ioOutputRegister = 0x05;
  constexpr uint8_t touchResetBit = 1U << 5;

  // Auf dem ESP32-P4 kann die Pinmatrix nach Panel-Autodetektion/Warmstart in
  // einer Lage verbleiben, in der die Expander antworten, der Touch aber
  // nicht. Ein einmaliger Wechsel ueber den bitgebangten Port gibt SDA/SCL
  // vollstaendig frei und richtet danach Hardware-I2C1 definiert neu ein.
  const bool hardwareReleased = M5.In_I2C.release();
  const bool softwareInitialized = m5gfx::i2c::init(-1, 31, 32).has_value();
  const bool softwareBusAlive = softwareInitialized && probeI2cAddress(-1, ioExpanderAddress);
  m5gfx::i2c::release(-1);
  const bool hardwareInitialized = M5.In_I2C.begin();

  uint8_t outputBefore = 0;
  const bool registerRead = M5.In_I2C.readRegister(ioExpanderAddress, ioOutputRegister,
                                                   &outputBefore, 1, 100000);
  Serial.printf("TOUCH_RECOVERY reason=%s bus_release=%s soft_init=%s soft_io=%s hw_init=%s io05_read=%s io05=0x%02X\n",
                reason, hardwareReleased ? "ok" : "failed",
                softwareInitialized ? "ok" : "failed", softwareBusAlive ? "ACK" : "NACK",
                hardwareInitialized ? "ok" : "failed", registerRead ? "ok" : "failed", outputBefore);

  // GPIO23 ist beim Tab5 der Touch-Interrupt-Pin. Er darf waehrend der
  // Recovery nicht als Ausgang verwendet werden. Der ST7123 wird allein
  // ueber TP_RST (Expander-Bit 5) zurueckgesetzt.
  const bool resetLow = M5.In_I2C.bitOff(ioExpanderAddress, ioOutputRegister,
                                         touchResetBit, 100000);
  M5.delay(20);
  const bool resetHigh = M5.In_I2C.bitOn(ioExpanderAddress, ioOutputRegister,
                                         touchResetBit, 100000);

  bool responded = false;
  for (uint8_t attempt = 0; attempt < 70 && !responded; ++attempt) {
    M5.delay(10);
    responded = touchControllerResponds();
  }
  bool driverReady = false;
  if (responded && M5.Display.touch()) {
    driverReady = M5.Display.touch()->init();
    M5.Touch.begin(&M5.Display);
  }
  uint8_t outputAfter = 0;
  M5.In_I2C.readRegister(ioExpanderAddress, ioOutputRegister, &outputAfter, 1, 100000);
  Serial.printf("TOUCH_RECOVERY reason=%s reset_low=%s reset_high=%s response=%s driver=%s io05=0x%02X\n",
                reason, resetLow ? "ok" : "failed", resetHigh ? "ok" : "failed",
                responded ? "yes" : "no", driverReady ? "ready" : "failed", outputAfter);
  return responded && driverReady;
}

static bool restoreTouchAfterMicrophone(const char* reason) {
  const bool controllerResponds = touchControllerResponds();
  if (!controllerResponds) {
    Serial.printf("TOUCH_AFTER_MIC reason=%s response=no recovery=1\n", reason);
    return recoverTouchController(reason);
  }

  // Mic.begin()/end() baut den Audio-I2S-Kanal neu auf. Der Touch-Controller
  // antwortet danach zwar noch, aber ein frischer GFX-/M5Unified-Bind stellt
  // sicher, dass keine alte Touch-Zustandsmaschine weiterverwendet wird.
  M5.Touch.end();
  const bool driverReady = M5.Display.touch() && M5.Display.touch()->init();
  M5.Touch.begin(driverReady ? &M5.Display : nullptr);
  Serial.printf("TOUCH_AFTER_MIC reason=%s response=yes driver=%s\n",
                reason, driverReady ? "ready" : "failed");
  return driverReady;
}

static bool restoreTouchAfterFullRefresh(const char* reason) {
  const bool controllerResponds = touchControllerResponds();
  if (!controllerResponds) {
    Serial.printf("TOUCH_AFTER_DISPLAY reason=%s response=no recovery=1\n", reason);
    return recoverTouchController(reason);
  }

  M5.Touch.end();
  const bool driverReady = M5.Display.touch() && M5.Display.touch()->init();
  M5.Touch.begin(driverReady ? &M5.Display : nullptr);
  Serial.printf("TOUCH_AFTER_DISPLAY reason=%s response=yes driver=%s\n",
                reason, driverReady ? "ready" : "failed");
  return driverReady;
}

static void drawHardwareTouchMarker(int16_t x, int16_t y, TouchAction action) {
  if (x < 0 || y < 0 || x >= M5.Display.width() || y >= M5.Display.height()) return;
  const uint16_t color = action == TouchAction::None ? TFT_MAGENTA : TFT_GREEN;
  M5.Display.fillCircle(x, y, 13, color);
  M5.Display.drawCircle(x, y, 20, TFT_WHITE);
  M5.Display.display();
}

static TouchAction readTouch(uint8_t& selectedTest) {
  // Zwei Hardwarepfade werden ausgewertet: zuerst M5Unified (von M5.update),
  // danach als Fallback ein direkter, bereits koordinatenkonvertierter
  // M5GFX-Read. Damit bleibt die Bedienung auch dann funktionsfaehig, wenn die
  // High-Level-Touch-Zustandsmaschine keine Details publiziert.
  static bool touchWasDown = false;
  static uint32_t lastDirectReadMs = 0;
  ++g_touchPollCount;
  const int8_t intLevel = static_cast<int8_t>(digitalRead(23));
  if (g_touchLastIntLevel < 0) {
    g_touchLastIntLevel = intLevel;
  } else if (intLevel != g_touchLastIntLevel) {
    ++g_touchIntTransitions;
    if (g_touchIntTransitions <= 10 || g_touchIntTransitions % 100 == 0) {
      Serial.printf("TOUCH_INT level=%d transitions=%lu poll=%lu\n", intLevel,
                    static_cast<unsigned long>(g_touchIntTransitions),
                    static_cast<unsigned long>(g_touchPollCount));
    }
    g_touchLastIntLevel = intLevel;
  }
  const uint8_t unifiedCount = M5.Touch.getCount();
  g_touchLastUnifiedCount = unifiedCount;
  int16_t touchX = -1;
  int16_t touchY = -1;
  uint16_t touchSize = 0;
  bool touchIsDown = false;
  const char* source = "none";

  if (unifiedCount > 0) {
    const auto detail = M5.Touch.getDetail(0);
    if (detail.isPressed()) {
      touchX = detail.x;
      touchY = detail.y;
      touchSize = detail.size;
      touchIsDown = true;
      source = "M5Unified";
    }
  }

  uint8_t directCount = 0;
  // Nicht bei jedem leeren M5Unified-Sample ein zweites Mal lesen: Der
  // ST7123 wird durch mehrere hundert I2C-Abfragen pro Sekunde instabil. Der
  // direkte Pfad ist nur der Notfallpfad, wenn M5Unified keinen GFX-Endpunkt
  // besitzt; im Normalbetrieb gibt es genau eine Controllerabfrage je Loop.
  const uint32_t nowMs = millis();
  const bool captureFallbackDue = g_view == View::Capturing && nowMs - lastDirectReadMs >= 20;
  if (!touchIsDown && M5.Display.touch() && (!M5.Touch.isEnabled() || captureFallbackDue)) {
    lastDirectReadMs = nowMs;
    m5gfx::touch_point_t point{};
    directCount = static_cast<uint8_t>(M5.Display.getTouch(&point, 1));
    if (directCount > 0) {
      touchX = point.x;
      touchY = point.y;
      touchSize = point.size;
      touchIsDown = true;
      source = "M5GFX-direct";
    }
  }
  g_touchLastDirectCount = directCount;

  if (!touchIsDown) {
    if (touchWasDown && g_touchDebugEnabled) {
      Serial.printf("TOUCH_RAW release polls=%lu events=%lu int23=%d\n",
                    static_cast<unsigned long>(g_touchPollCount),
                    static_cast<unsigned long>(g_touchHardwareEvents), digitalRead(23));
    }
    touchWasDown = false;
    return TouchAction::None;
  }
  if (touchWasDown) return TouchAction::None;
  touchWasDown = true;
  ++g_touchHardwareEvents;
  g_touchLastX = touchX;
  g_touchLastY = touchY;
  g_touchLastSource = source;
  const TouchAction action = hitTestTouch(touchX, touchY, selectedTest);
  Serial.printf("TOUCH_RAW down source=%s unified=%u direct=%u x=%d y=%d size=%u int23=%d\n",
                source, static_cast<unsigned>(unifiedCount), static_cast<unsigned>(directCount),
                touchX, touchY, static_cast<unsigned>(touchSize), digitalRead(23));
  Serial.printf("TOUCH_HW x=%d y=%d action=%u selected=%u\n", touchX, touchY,
                static_cast<unsigned>(action),
                action == TouchAction::None ? 0U : static_cast<unsigned>(selectedTest + 1));
  drawHardwareTouchMarker(touchX, touchY, action);
  // Das akustische Feedback erfolgt genau einmal in handleTouchAction().
  // Ein zusaetzlicher Ton hier wuerde bei einer Feldwahl zwei I2S-Ausgaben
  // direkt hintereinander starten und den naechsten Touch-Poll stoeren.
  return action;
}

static void handleTouchAction(TouchAction action, uint8_t selected);

static void writeLe16(File& file, uint16_t value) { uint8_t b[2] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)}; file.write(b, 2); }
static void writeLe32(File& file, uint32_t value) { uint8_t b[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)}; file.write(b, 4); }

static bool writeWav() {
  if (!g_pcm || !g_validFrames || g_lastCaptureTest != static_cast<int8_t>(g_test)) return false;
  if (!SD_MMC.setPins(43, 44, 39, 40, 41, 42) || !SD_MMC.begin("/sdcard", false)) return false;
  File file = SD_MMC.open("/audio_probe.wav", FILE_WRITE);
  if (!file) { SD_MMC.end(); return false; }
  const uint32_t bytes = g_validFrames * 4U;
  file.write(reinterpret_cast<const uint8_t*>("RIFF"), 4); writeLe32(file, 36U + bytes);
  file.write(reinterpret_cast<const uint8_t*>("WAVEfmt "), 8); writeLe32(file, 16); writeLe16(file, 1); writeLe16(file, 2);
  writeLe32(file, kSampleRate); writeLe32(file, kSampleRate * 4U); writeLe16(file, 4); writeLe16(file, 16);
  file.write(reinterpret_cast<const uint8_t*>("data"), 4); writeLe32(file, bytes);
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(g_pcm), bytes);
  file.flush(); file.close(); SD_MMC.end(); return written == bytes;
}

static void analyzeCapture() {
  TestResult& result = g_results[g_test]; result = TestResult{}; result.available = true; result.frames = g_captureFrames; result.captureUs = g_captureAudioUs;
  result.captureUs = result.captureUs ? result.captureUs : 1;
  for (uint32_t frame = 0; frame < g_captureFrames; ++frame) { collectStats(result.ch0, g_pcm[frame * 2]); collectStats(result.ch1, g_pcm[frame * 2 + 1]); }
  if (result.frames == 0) {
    result.ch0.min = result.ch0.max = 0;
    result.ch1.min = result.ch1.max = 0;
  }
  const double rate = 1000000.0 * result.frames / result.captureUs;
  const bool signal = result.frames > 0 && result.ch0.max != result.ch0.min && result.ch1.max != result.ch1.min;
  result.warning = result.ch0.nearFullScale || result.ch1.nearFullScale;
  result.valid = signal && rate >= kSampleRate * .98 && rate <= kSampleRate * 1.02 && !result.warning;
  g_status[g_test] = result.valid ? TestStatus::Done : result.warning ? TestStatus::Warning : TestStatus::Failed;
  Serial.printf("RESULT TEST=%u/%u frames=%lu duration_s=%.6f effective_rate=%.3f status=%s\n", static_cast<unsigned>(g_test + 1), static_cast<unsigned>(kTestCount), static_cast<unsigned long>(result.frames), result.captureUs / 1000000.0, rate, statusText(g_status[g_test]));
  const double ch0Rms = result.frames ? std::sqrt(static_cast<double>(result.ch0.squares) / result.frames) : 0.0;
  const double ch1Rms = result.frames ? std::sqrt(static_cast<double>(result.ch1.squares) / result.frames) : 0.0;
  const double ch0Dc = result.frames ? static_cast<double>(result.ch0.sum) / result.frames : 0.0;
  const double ch1Dc = result.frames ? static_cast<double>(result.ch1.sum) / result.frames : 0.0;
  Serial.printf("channel_0 min=%d max=%d RMS=%.2f DC=%.2f near_full_scale=%lu clipping_events=%lu\n", result.ch0.min, result.ch0.max, ch0Rms, ch0Dc, static_cast<unsigned long>(result.ch0.nearFullScale), static_cast<unsigned long>(result.ch0.clippingEvents));
  Serial.printf("channel_1 min=%d max=%d RMS=%.2f DC=%.2f near_full_scale=%lu clipping_events=%lu\n", result.ch1.min, result.ch1.max, ch1Rms, ch1Dc, static_cast<unsigned long>(result.ch1.nearFullScale), static_cast<unsigned long>(result.ch1.clippingEvents));
  g_view = View::Result; g_feedback = result.valid ? "TEST FERTIG" : "WIEDERHOLEN EMPFOHLEN"; drawScreen();
}

static void finishCapture() { M5.Mic.end(); restoreTouchAfterMicrophone("capture_finished"); g_validFrames = g_captureFrames; g_lastCaptureTest = static_cast<int8_t>(g_test); analyzeCapture(); }
static void resumeCapture() { g_stopRequested = false; g_view = View::Capturing; g_feedback = "AUFNAHME WIRD FORTGESETZT"; M5.Mic.begin(); drawScreen(); }
static void confirmEarlyStop() { M5.Mic.end(); restoreTouchAfterMicrophone("capture_stopped"); g_validFrames = g_captureFrames; g_lastCaptureTest = static_cast<int8_t>(g_test); g_status[g_test] = TestStatus::Failed; analyzeCapture(); }

static void capturePoll() {
  if (g_view != View::Capturing) return;
  const uint32_t elapsedSeconds = g_captureFrames / kSampleRate;
  if (elapsedSeconds != g_lastUiSecond) { g_lastUiSecond = elapsedSeconds; drawScreen(); }
  if (g_stopRequested && g_captureFrames >= kMinDurationSeconds * kSampleRate) { finishCapture(); return; }
  if (g_captureFrames >= kMaxFrames) { finishCapture(); return; }
  const uint32_t remaining = kMaxFrames - g_captureFrames;
  const uint32_t frames = remaining < kChunkFrames ? remaining : kChunkFrames;
  const uint64_t chunkStartedUs = micros();
  if (!M5.Mic.record(g_chunk, frames * 2U, kSampleRate, true)) { M5.Mic.end(); restoreTouchAfterMicrophone("capture_error"); g_error = "MIKROFON AUFNAHME FEHLER"; g_status[g_test] = TestStatus::Failed; g_view = View::Error; drawScreen(); return; }
  while (!M5.Mic.isRecording()) {
    M5.update();
    touchPoll();
    M5.delay(1);
  }
  while (M5.Mic.isRecording()) {
    M5.update();
    touchPoll();
    M5.delay(1);
  }
  g_captureAudioUs += micros() - chunkStartedUs;
  std::memcpy(g_pcm + g_captureFrames * 2U, g_chunk, frames * 2U * sizeof(int16_t));
  g_captureFrames += frames;
}

static void startCapture() {
  g_captureFrames = 0; g_validFrames = 0; g_stopRequested = false; g_wavWritten = false; g_captureStartedMs = millis(); g_captureStartedUs = micros(); g_captureAudioUs = 0; g_lastUiSecond = UINT32_MAX; g_status[g_test] = TestStatus::Running; g_view = View::Capturing; g_feedback = "AUFNAHME STARTET"; drawScreen();
  if (!M5.Mic.begin()) { restoreTouchAfterMicrophone("mic_start_error"); g_error = "MIKROFON START FEHLER"; g_status[g_test] = TestStatus::Failed; g_view = View::Error; drawScreen(); }
}

static void listTests() {
  Serial.printf("LIST completed=%u/%u selected=%u\n", static_cast<unsigned>(completedCount()), static_cast<unsigned>(kTestCount), static_cast<unsigned>(g_test + 1));
  for (uint8_t i = 0; i < kTestCount; ++i) Serial.printf("TEST %u %s status=%s\n", static_cast<unsigned>(i + 1), kTestTitles[i], statusText(g_status[i]));
}

static void printStatus() {
  Serial.printf("STATE=%s TEST=%u/%u status=%s frames=%lu wav=%s psram=%s touch=%s driver=%s events=%lu accepted=%lu rejected=%lu display=%dx%d rotation=%u\n", g_view == View::List ? "LIST" : g_view == View::Detail ? "DETAIL" : g_view == View::Capturing ? "CAPTURING" : g_view == View::Result ? "RESULT" : g_view == View::Error ? "ERROR" : g_view == View::StopConfirm ? "STOP_CONFIRM" : "ABORT_CONFIRM", static_cast<unsigned>(g_test + 1), static_cast<unsigned>(kTestCount), statusText(g_status[g_test]), static_cast<unsigned long>(g_captureFrames), g_wavWritten ? "yes" : "no", g_pcm && g_chunk ? "reserved" : "missing", M5.Touch.isEnabled() ? "enabled" : "disabled", M5.Display.touch() ? "present" : "missing", static_cast<unsigned long>(g_touchHardwareEvents), static_cast<unsigned long>(g_touchAcceptedEvents), static_cast<unsigned long>(g_touchRejectedEvents), M5.Display.width(), M5.Display.height(), static_cast<unsigned>(M5.Display.getRotation()));
}

static void printTouchDiagnostics(const char* reason) {
  Serial.printf("TOUCH_DIAG reason=%s debug=%s board=%u displays=%u enabled=%s driver=%s int23=%d int_edges=%lu unified=%u direct=%u polls=%lu events=%lu accepted=%lu rejected=%lu last_source=%s last_x=%d last_y=%d\n",
                reason, g_touchDebugEnabled ? "on" : "off",
                static_cast<unsigned>(M5.Display.getBoard()),
                static_cast<unsigned>(M5.getDisplayCount()),
                M5.Touch.isEnabled() ? "yes" : "no", M5.Display.touch() ? "present" : "missing",
                digitalRead(23), static_cast<unsigned long>(g_touchIntTransitions),
                static_cast<unsigned>(g_touchLastUnifiedCount),
                static_cast<unsigned>(g_touchLastDirectCount),
                static_cast<unsigned long>(g_touchPollCount),
                static_cast<unsigned long>(g_touchHardwareEvents),
                static_cast<unsigned long>(g_touchAcceptedEvents),
                static_cast<unsigned long>(g_touchRejectedEvents), g_touchLastSource,
                g_touchLastX, g_touchLastY);
}

static void touchDiagnosticHeartbeat() {
  if (!g_touchDebugEnabled) return;
  const uint32_t now = millis();
  if (now - g_touchLastHeartbeatMs < 5000) return;
  g_touchLastHeartbeatMs = now;
  printTouchDiagnostics("heartbeat");
}

static void printTouchBusDiagnostics() {
  constexpr uint8_t stAddress = 0x55;
  constexpr uint8_t gtAddress1 = 0x14;
  constexpr uint8_t gtAddress2 = 0x5D;
  constexpr uint8_t ioExpander1 = 0x43;
  constexpr uint8_t ioExpander2 = 0x44;

  const int hardwarePort = static_cast<int>(M5.In_I2C.getPort());
  const bool hwSt = probeI2cAddress(hardwarePort, stAddress);
  const bool hwGt1 = probeI2cAddress(hardwarePort, gtAddress1);
  const bool hwGt2 = probeI2cAddress(hardwarePort, gtAddress2);
  const bool hwIo1 = probeI2cAddress(hardwarePort, ioExpander1);
  const bool hwIo2 = probeI2cAddress(hardwarePort, ioExpander2);
  Serial.printf("TOUCH_BUS mode=hardware port=%d sda=%d scl=%d io43=%s io44=%s st55=%s gt14=%s gt5d=%s\n",
                hardwarePort, M5.In_I2C.getSDA(), M5.In_I2C.getSCL(),
                hwIo1 ? "ACK" : "NACK", hwIo2 ? "ACK" : "NACK", hwSt ? "ACK" : "NACK",
                hwGt1 ? "ACK" : "NACK", hwGt2 ? "ACK" : "NACK");

  // Der Tab5 wird von M5GFX ueber Software-I2C erkannt. Derselbe zweite
  // Messpfad zeigt, ob nur der ESP32-P4-Hardwareport ausgefallen ist oder der
  // Controller selbst nicht antwortet. Danach wird Port 1 wiederhergestellt.
  m5gfx::i2c::release(hardwarePort);
  const bool softInitialized = m5gfx::i2c::init(-1, 31, 32).has_value();
  const bool stAck = softInitialized && probeI2cAddress(-1, stAddress);
  const bool gt1Ack = softInitialized && probeI2cAddress(-1, gtAddress1);
  const bool gt2Ack = softInitialized && probeI2cAddress(-1, gtAddress2);
  const bool softIo1 = softInitialized && probeI2cAddress(-1, ioExpander1);
  const bool softIo2 = softInitialized && probeI2cAddress(-1, ioExpander2);
  Serial.printf("TOUCH_BUS mode=software port=-1 init=%s io43=%s io44=%s st55=%s gt14=%s gt5d=%s\n",
                softInitialized ? "ok" : "failed", softIo1 ? "ACK" : "NACK",
                softIo2 ? "ACK" : "NACK", stAck ? "ACK" : "NACK",
                gt1Ack ? "ACK" : "NACK", gt2Ack ? "ACK" : "NACK");

  if (stAck) {
    const uint8_t reg[2] = {0x00, 0x00};
    uint8_t firmwareVersion = 0;
    const bool readOk = m5gfx::i2c::transactionWriteRead(
      -1, stAddress, reg, sizeof(reg), &firmwareVersion, 1, 100000).has_value();
    Serial.printf("TOUCH_BUS controller=ST71xx fw_read=%s fw=%u\n",
                  readOk ? "ok" : "failed", static_cast<unsigned>(firmwareVersion));
  } else if (gt1Ack || gt2Ack) {
    const uint8_t address = gt1Ack ? gtAddress1 : gtAddress2;
    const uint8_t reg[2] = {0x81, 0x40};
    uint8_t productId[4]{};
    const bool readOk = m5gfx::i2c::transactionWriteRead(
      -1, address, reg, sizeof(reg), productId, sizeof(productId), 100000).has_value();
    Serial.printf("TOUCH_BUS controller=GT911 id_read=%s id=%02X%02X%02X%02X\n",
                  readOk ? "ok" : "failed", productId[0], productId[1], productId[2], productId[3]);
  } else {
    Serial.println("TOUCH_BUS controller=NO_RESPONSE");
  }
  m5gfx::i2c::release(-1);
  const bool restored = m5gfx::i2c::init(hardwarePort, 31, 32).has_value();
  Serial.printf("TOUCH_BUS hardware_restore=%s\n", restored ? "ok" : "failed");
}

static void printTouchMap() {
  RowLayout rows[kTestCount]; computeRowLayout(rows);
  const int16_t x = outerMargin(); const int16_t width = M5.Display.width() - 2 * outerMargin();
  for (uint8_t i = 0; i < kTestCount; ++i) {
    const Rect cell{ x, rows[i].y, width, static_cast<int16_t>(rows[i].height - 6) };
    Serial.printf("TOUCHMAP ROW=%u CENTER=%d,%d RECT=%d,%d,%d,%d\n", static_cast<unsigned>(i + 1), cell.x + cell.w / 2, cell.y + cell.h / 2, cell.x, cell.y, cell.w, cell.h);
  }
  Serial.printf("TOUCHMAP ACTIVE=%u STATE=%s WHOLE_ROW_BUTTON=yes\n", static_cast<unsigned>(g_test + 1),
                g_view == View::List ? "LIST" : g_view == View::Detail ? "DETAIL" : g_view == View::Capturing ? "CAPTURING" : g_view == View::Result ? "RESULT" : g_view == View::Error ? "ERROR" : g_view == View::StopConfirm ? "STOP_CONFIRM" : "ABORT_CONFIRM");
}

static void printHelp() { Serial.println("HELP: SELECT 1..6 | START | STOP | LIST | REPEAT | WAV | STATUS | ABORT CONFIRM | TOUCH x y | TOUCHMAP | TOUCHDIAG [ON|OFF|TONE|BUS]"); }

static void requestAbort() { if (g_view == View::Capturing) { M5.Mic.end(); g_status[g_test] = TestStatus::Open; } g_abortReturnView = g_view; g_view = View::AbortConfirm; g_feedback = "ABBRUCH BESTAETIGEN"; drawScreen(); Serial.printf("STATE=ABORT_CONFIRM TEST=%u/6\n", static_cast<unsigned>(g_test + 1)); }
static void confirmAbort() { g_view = View::List; g_stopRequested = false; g_feedback = "ZUR TESTLISTE"; drawScreen(); Serial.printf("STATE=LIST TEST=%u/6\n", static_cast<unsigned>(g_test + 1)); }

static void command(const char* input) {
  if (!std::strcmp(input, "HELP")) printHelp();
  else if (!std::strcmp(input, "TOUCHDIAG")) printTouchDiagnostics("command");
  else if (!std::strcmp(input, "TOUCHDIAG ON")) { g_touchDebugEnabled = true; printTouchDiagnostics("enabled"); }
  else if (!std::strcmp(input, "TOUCHDIAG OFF")) { g_touchDebugEnabled = false; printTouchDiagnostics("disabled"); }
  else if (!std::strcmp(input, "TOUCHDIAG TONE")) { Serial.println("TOUCH_DIAG tone=disabled"); }
  else if (!std::strcmp(input, "TOUCHDIAG BUS")) printTouchBusDiagnostics();
  else if (!std::strcmp(input, "TOUCHMAP")) printTouchMap();
  else if (!std::strncmp(input, "TOUCH ", 6)) {
    int x = 0, y = 0;
    if (std::sscanf(input + 6, "%d %d", &x, &y) == 2) {
      uint8_t selected = 0;
      const TouchAction action = hitTestTouch(static_cast<int16_t>(x), static_cast<int16_t>(y), selected);
      Serial.printf("TOUCH_SERIAL x=%d y=%d action=%u selected=%u\n", x, y, static_cast<unsigned>(action), static_cast<unsigned>(selected + 1));
      handleTouchAction(action, selected);
    } else Serial.println("REJECTED=TOUCH EXPECTED_X_Y");
  }
  else if (!std::strcmp(input, "LIST")) { if (g_view == View::Capturing) Serial.println("REJECTED=LIST CAPTURE_BUSY"); else { g_view = View::List; g_feedback = "TESTLISTE"; drawScreen(); listTests(); } }
  else if (!std::strcmp(input, "STATUS")) printStatus();
  else if (!std::strcmp(input, "START")) { if (g_view == View::Detail || g_view == View::Result) startCapture(); else if (g_view == View::StopConfirm) resumeCapture(); else Serial.println("REJECTED=START SELECT_TEST_FIRST"); }
  else if (!std::strcmp(input, "STOP")) { if (g_view == View::Capturing) { if (g_captureFrames < kMinDurationSeconds * kSampleRate) { g_stopRequested = true; M5.Mic.end(); restoreTouchAfterMicrophone("stop_confirm"); g_view = View::StopConfirm; g_feedback = "STOPP BESTAETIGEN"; drawScreen(); } else { g_stopRequested = true; } } else if (g_view == View::StopConfirm) confirmEarlyStop(); else Serial.println("REJECTED=STOP NOT_CAPTURING"); }
  else if (!std::strcmp(input, "REPEAT")) { if (g_view == View::Result || g_view == View::Error) { g_view = View::Detail; g_feedback = "TEST BEREIT ZUR WIEDERHOLUNG"; drawScreen(); } else Serial.println("REJECTED=REPEAT NO_RESULT"); }
  else if (!std::strcmp(input, "WAV")) { if (g_view == View::Result && g_lastCaptureTest == static_cast<int8_t>(g_test)) { g_feedback = "WAV WIRD GESPEICHERT"; drawScreen(); g_wavWritten = writeWav(); if (g_wavWritten) { g_feedback = "WAV GESPEICHERT"; Serial.println("WAV PASS /audio_probe.wav"); } else { g_error = "SD-FEHLER"; g_view = View::Error; Serial.println("WAV FAIL"); } drawScreen(); } else Serial.println("REJECTED=WAV NO_CURRENT_CAPTURE"); }
  else if (!std::strncmp(input, "SELECT ", 7)) { int selected = std::atoi(input + 7); if (selected >= 1 && selected <= kTestCount && g_view != View::Capturing) { g_test = static_cast<uint8_t>(selected - 1); g_view = View::Detail; g_feedback = "TEST GEWAEHLT"; drawScreen(); Serial.printf("STATE=DETAIL TEST=%d/6\n", selected); } else Serial.println("REJECTED=SELECT INVALID"); }
  else if (!std::strcmp(input, "ABORT CONFIRM")) { if (g_view == View::AbortConfirm) confirmAbort(); else Serial.println("REJECTED=ABORT NO_CONFIRMATION"); }
  else if (!std::strcmp(input, "ABORT")) requestAbort();
  else if (input[0]) Serial.printf("UNKNOWN=%s\n", input);
}

static void serialPoll() { while (Serial.available()) { const char c = static_cast<char>(Serial.read()); if (c == '\r' || c == '\n') { g_commandLine[g_commandLength] = 0; command(g_commandLine); g_commandLength = 0; } else if (g_commandLength + 1 < sizeof(g_commandLine) && c >= 0x20 && c <= 0x7e) g_commandLine[g_commandLength++] = c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; else if (g_commandLength + 1 >= sizeof(g_commandLine)) { g_commandLength = 0; Serial.println("ERROR=COMMAND_TOO_LONG"); } } }

static void handleTouchAction(TouchAction action, uint8_t selected) {
  if (action == TouchAction::Select) {
    const bool sameField = g_view != View::List && selected == g_test;
    const bool focusLocked = g_view == View::Capturing || g_view == View::StopConfirm || g_view == View::AbortConfirm;
  if (!sameField && focusLocked) {
      ++g_touchRejectedEvents;
      g_feedback = "ERST AKTIVEN TEST BEENDEN";
      drawScreen();
      Serial.printf("TOUCH_REJECTED focus_locked active=%u requested=%u\n", static_cast<unsigned>(g_test + 1), static_cast<unsigned>(selected + 1));
      return;
    }

    ++g_touchAcceptedEvents;

    g_pressedTest = static_cast<int8_t>(selected);
    g_feedback = "FELD ANGENOMMEN";
    drawScreen();
    if (g_view != View::Capturing) M5.delay(180);
    g_pressedTest = -1;

    if (!sameField) {
      g_test = selected;
      g_view = View::Detail;
      g_feedback = "TEST GEWAEHLT";
      drawScreen();
      Serial.printf("STATE=DETAIL TEST=%u/6\n", static_cast<unsigned>(g_test + 1));
      return;
    }

    if (g_view == View::Detail) startCapture();
    else if (g_view == View::Capturing) command("STOP");
    else if (g_view == View::StopConfirm) { g_feedback = "STOPP BESTAETIGT"; confirmEarlyStop(); }
    else if (g_view == View::Result || g_view == View::Error) command("REPEAT");
    else if (g_view == View::AbortConfirm) confirmAbort();
    return;
  }
  if (!actionEnabled(action)) return;
  g_pressed = action; g_feedback = "TOUCH ANGENOMMEN"; drawScreen(); if (g_view != View::Capturing) M5.delay(180); g_pressed = TouchAction::None;
  if (g_view == View::Detail && action == TouchAction::Left) { g_view = View::List; g_feedback = "ZUR TESTLISTE"; drawScreen(); }
  else if (g_view == View::Detail && action == TouchAction::Middle) startCapture();
  else if (g_view == View::Detail && action == TouchAction::Right) requestAbort();
  else if (g_view == View::Capturing && action == TouchAction::Middle) command("STOP");
  else if (g_view == View::StopConfirm && action == TouchAction::Middle) resumeCapture();
  else if (g_view == View::StopConfirm && action == TouchAction::Right) { g_feedback = "STOPP BESTAETIGT"; confirmEarlyStop(); }
  else if (g_view == View::Result && action == TouchAction::Left) { g_view = View::List; g_feedback = "ZUR TESTLISTE"; drawScreen(); }
  else if (g_view == View::Result && action == TouchAction::Middle) command("REPEAT");
  else if (g_view == View::Result && action == TouchAction::Right) command("WAV");
  else if (g_view == View::Error && action == TouchAction::Left) { g_view = View::List; drawScreen(); }
  else if (g_view == View::Error && action == TouchAction::Middle) command("REPEAT");
  else if (g_view == View::Error && action == TouchAction::Right) requestAbort();
  else if (g_view == View::AbortConfirm && action == TouchAction::Left) confirmAbort();
  else if (g_view == View::AbortConfirm && action == TouchAction::Middle) { g_view = g_abortReturnView; g_feedback = "TEST WIRD FORTGESETZT"; drawScreen(); }
  else if (g_view == View::AbortConfirm && action == TouchAction::Right) confirmAbort();
}

static void touchPoll() {
  uint8_t selected = 0;
  const TouchAction action = readTouch(selected);
  handleTouchAction(action, selected);
}

namespace {
constexpr uint8_t kDisplayInitAttempts = 5;
constexpr uint32_t kDisplayInitRetryMs = 250;
RTC_DATA_ATTR uint8_t g_displayBootRecoveryAttempted = 0;

bool ensureTab5DisplayAndTouchReady() {
  if (M5.Display.width() > 0 && M5.Display.height() > 0 && M5.Touch.isEnabled()) {
    g_displayBootRecoveryAttempted = 0;
    return true;
  }

  // M5GFX kann die Tab5-Panelerkennung nach einem fehlgeschlagenen ersten
  // Power-up nicht vollstaendig wiederholen. Ein einziger Neustart entspricht
  // dem bewaehrten Recovery-Pfad des Analyzer-Projekts.
  if (g_displayBootRecoveryAttempted == 0) {
    g_displayBootRecoveryAttempted = 1;
    Serial.println("TAB5_INIT_RECOVERY restart=1");
    Serial.flush();
    delay(100);
    ESP.restart();
  }

  for (uint8_t attempt = 1; attempt <= kDisplayInitAttempts; ++attempt) {
    delay(kDisplayInitRetryMs);
    if (!M5.Display.init() || M5.Display.width() <= 0 || M5.Display.height() <= 0) continue;
    if (M5.getDisplayCount() == 0) M5.addDisplay(M5.Display);
    M5.Touch.begin(M5.Display.touch() ? &M5.Display : nullptr);
    if (M5.Touch.isEnabled()) {
      g_displayBootRecoveryAttempted = 0;
      Serial.printf("TAB5_INIT_RECOVERY attempt=%u result=ready\n", static_cast<unsigned>(attempt));
      return true;
    }
  }
  Serial.println("TAB5_INIT_RECOVERY result=failed");
  return false;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  auto config = M5.config(); config.clear_display = true; M5.begin(config);
  const bool displayAndTouchReady = ensureTab5DisplayAndTouchReady();
  auto mic = M5.Mic.config(); mic.sample_rate = kSampleRate; mic.input_channel = m5::input_channel_t::input_stereo; mic.over_sampling = 1; mic.magnification = 2; mic.noise_filter_level = 0; M5.Mic.config(mic);
  g_pcm = static_cast<int16_t*>(heap_caps_malloc(static_cast<size_t>(kMaxSamples) * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  g_chunk = static_cast<int16_t*>(heap_caps_malloc(static_cast<size_t>(kChunkFrames) * 2U * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  Serial.printf("M5Tab5_Audio_Probe UI=static-test-list rate=16000 stereo min=20s max=30s touch=%s driver=%s display=%dx%d rotation=%u\n", M5.Touch.isEnabled() ? "enabled" : "disabled", M5.Display.touch() ? "present" : "missing", M5.Display.width(), M5.Display.height(), static_cast<unsigned>(M5.Display.getRotation()));
  printTouchDiagnostics("boot");
  if (!displayAndTouchReady || !M5.Mic.isEnabled() || !g_pcm || !g_chunk) { g_error = "DISPLAY/TOUCH/MIKROFON/PSRAM FEHLER"; g_view = View::Error; }
  drawScreen();
  if (!touchControllerResponds() && !recoverTouchController("after_first_draw_retry")) {
    g_error = "TOUCHCONTROLLER ANTWORTET NICHT";
    g_view = View::Error;
    drawScreen();
  }
}

void loop() {
  M5.update();
  serialPoll();
  capturePoll();
  touchPoll();
  touchDiagnosticHeartbeat();
  M5.delay(15);
}
