#include "Dashboard.h"

#include <cmath>
#include "Console.h"

Dashboard dashboard;

namespace {
constexpr uint32_t kRefreshMs = 100;
constexpr int16_t kTabHeight = 52;
constexpr int16_t kStatusBarHeight = 58;
constexpr int16_t kContentTop = kTabHeight + kStatusBarHeight;
constexpr int16_t kMargin = 12;

// Digifant 1.7 Kennfeld-Tabelle fuer Formel 0x8C (Temperatur in °C)
const uint8_t kTempTable8C[17] = {
  0xA0, 0x64, 0x50, 0x44, 0x3A, 0x32, 0x2C, 0x26, 0x21, 0x1B, 0x16, 0x10, 0x0B, 0x04, 0x00, 0x00, 0x00
};

float decodeTemp8C(uint8_t mwb) {
  uint8_t idx = mwb >> 4;
  if (idx > 15) idx = 15;
  const int16_t left = static_cast<int16_t>(kTempTable8C[idx]);
  const int16_t right = (idx < 16) ? static_cast<int16_t>(kTempTable8C[idx + 1]) : static_cast<int16_t>(kTempTable8C[16]);
  const uint8_t frac = mwb & 0x0F;
  const float interpolated = static_cast<float>(left) + static_cast<float>(right - left) * (frac / 16.0f);
  return interpolated - 40.0f;
}

void drawCardFrame(LGFX_Sprite &s, int16_t w, int16_t h, uint16_t color,
                   const char *label, const char *sub = nullptr) {
  s.fillScreen(TFT_BLACK);
  s.fillRoundRect(0, 0, w, h, 12, TFT_DARKGREY);
  s.drawRoundRect(0, 0, w, h, 12, color);
  s.drawRoundRect(1, 1, w - 2, h - 2, 12, color);

  s.setTextSize(2);
  s.setTextColor(TFT_WHITE, TFT_DARKGREY);
  s.setCursor(14, 12);
  s.print(label);

  if (sub != nullptr) {
    s.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    s.setCursor(14, 32);
    s.print(sub);
  }
}

void drawFooter(LGFX_Sprite &s, int16_t h, const char *rawText) {
  s.setTextSize(2);
  s.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  s.setCursor(14, h - 26);
  s.print(rawText);
}
}  // namespace

float Dashboard::rawToCoolantTemp(uint8_t raw) {
  return decodeTemp8C(raw);
}

float Dashboard::rawToIatTemp(uint8_t raw) {
  return decodeTemp8C(raw);
}

float Dashboard::rawToG69Deg(uint8_t raw) {
  float deg = static_cast<float>(raw) * (80.0f / 85.0f);
  if (deg > 90.0f) deg = 90.0f;
  return deg;
}

void Dashboard::begin() {
  if (M5.Display.height() > M5.Display.width()) {
    M5.Display.setRotation((M5.Display.getRotation() + 1) & 3);
  }

  // Kachel-Sprites im PSRAM anlegen (4 Spalten x 2 Zeilen = 8 Kacheln)
  const int16_t gap = 12;
  const int16_t cols = 4;
  const int16_t tileW = (M5.Display.width() - 2 * kMargin - (cols - 1) * gap) / cols;
  const int16_t tileH = (M5.Display.height() - kContentTop - kMargin - gap) / 2;

  _tileSprite.setPsram(true);
  _tileSprite.createSprite(tileW, tileH);

  _statusSprite.setPsram(true);
  _statusSprite.createSprite(M5.Display.width(), kStatusBarHeight);

  // Tab 2 Sprites im PSRAM fuer 100% flackerfreie ECU-Kacheln und Live-Log-Terminal
  const int16_t topH = 210;
  const int16_t cardW = (M5.Display.width() - 2 * kMargin - gap) / 2;
  _tab2CardSprite.setPsram(true);
  _tab2CardSprite.createSprite(cardW, topH);

  const int16_t logY = kContentTop + 8 + topH + 12;
  const int16_t logH = M5.Display.height() - logY - kMargin;
  const int16_t logW = M5.Display.width() - 2 * kMargin;
  _logSprite.setPsram(true);
  _logSprite.createSprite(logW, logH);

  // Tab 3 Messschrieb-Sprite im PSRAM fuer 100% flackerfreies Rendern
  const int16_t scopeY = kContentTop + 8;
  const int16_t scopeH = M5.Display.height() - scopeY - kMargin;
  const int16_t scopeW = M5.Display.width() - 2 * kMargin;
  _scopeSprite.setPsram(true);
  _scopeSprite.createSprite(scopeW, scopeH);

  _spritesCreated = true;
  _needFullClear = true;
  _dirty = true;
  draw();
}

void Dashboard::handleTouch() {
  if (M5.Touch.getCount() == 0) {
    return;
  }
  const auto &touch = M5.Touch.getDetail(0);
  if (!touch.wasPressed()) {
    return;
  }
  if (touch.y >= kTabHeight || M5.Display.width() <= 0) {
    return;
  }
  const int16_t tabWidth = M5.Display.width() / 3;
  uint8_t tab = static_cast<uint8_t>(touch.x / tabWidth);
  selectTab(tab > 2 ? 2 : tab);
}

void Dashboard::update() {
  handleTouch();
  if (!_dirty || millis() - _lastDrawMs < kRefreshMs) {
    return;
  }
  draw();
}

void Dashboard::setGroup000(uint16_t rpm, uint8_t coolantRaw, uint8_t iatRaw,
                            uint8_t statusRaw, uint8_t runFlagRaw,
                            uint8_t lambdaRaw, uint8_t injRaw) {
  _rpm = rpm;
  _coolantRaw = coolantRaw;
  _iatRaw = iatRaw;
  _statusRaw = statusRaw;
  _runFlagRaw = runFlagRaw;
  _lambdaRaw = lambdaRaw;
  _injRaw = injRaw;
  _running = (runFlagRaw & 0x80) == 0;
  _dirty = true;
}

void Dashboard::setBattery(float volts) {
  _battery = volts;
  _dirty = true;
}

void Dashboard::setG69(uint8_t raw) {
  _g69Raw = raw;
  _dirty = true;
}

void Dashboard::setLambda(uint8_t raw) {
  _lambdaRaw = raw;
  _dirty = true;
}

void Dashboard::setInjTime(uint8_t raw) {
  _injRaw = raw;
  _dirty = true;
}

void Dashboard::setEcuInfo(const String &info) {
  _ecuInfo = info;
  _dirty = true;
}

void Dashboard::setMode(const String &mode) {
  _mode = mode;
  _dirty = true;
}

void Dashboard::setState(const String &state) {
  _state = state;
  _dirty = true;
}

void Dashboard::setStage(ConnStage stage, const String &detail) {
  _stage = stage;
  if (detail.length() > 0) {
    _state = detail;
  }
  _dirty = true;
}

void Dashboard::draw() {
  _lastDrawMs = millis();
  _dirty = false;

  M5.Display.startWrite();
  if (_needFullClear || _lastDrawnTab != _tab) {
    _needFullClear = false;
    _lastDrawnTab = _tab;
    M5.Display.fillScreen(TFT_BLACK);
    drawTabs();
  }

  drawStatusBar();

  if (_tab == 0) drawValues();
  else if (_tab == 1) drawInfo();
  else drawScope();

  M5.Display.endWrite();
}

void Dashboard::drawTabs() {
  const int16_t width = M5.Display.width() / 3;
  const char *names[] = {"1. WERTE", "2. ECU / LOG", "3. MESSSCHRIEB"};
  for (uint8_t i = 0; i < 3; ++i) {
    const int16_t x = i * width;
    const bool active = (i == _tab);
    M5.Display.fillRect(x, 0, width - 2, kTabHeight,
                        active ? TFT_DARKCYAN : TFT_DARKGREY);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE, active ? TFT_DARKCYAN : TFT_DARKGREY);
    M5.Display.setCursor(x + 24, 14);
    M5.Display.print(names[i]);
  }
}

void Dashboard::drawStatusBar() {
  _statusSprite.fillScreen(TFT_NAVY);

  const char *steps[] = {"1. BEREIT", "2. 5-BAUD", "3. HANDSHAKE", "4. IDENT", "5. VERBUNDEN"};
  const uint8_t currentStepIdx = static_cast<uint8_t>(_stage);
  const int16_t stepGap = 6;
  const int16_t totalStepW = M5.Display.width() - 2 * kMargin;
  const int16_t stepW = (totalStepW - 4 * stepGap) / 5;
  const int16_t stepH = 26;
  const int16_t stepY = 6;

  for (uint8_t i = 0; i < 5; ++i) {
    const int16_t sx = kMargin + i * (stepW + stepGap);
    uint16_t bg = TFT_DARKGREY;
    uint16_t fg = TFT_LIGHTGREY;
    uint16_t border = TFT_LIGHTGREY;

    if (_stage == ConnStage::FEHLER) {
      if (i == currentStepIdx || i == 4) {
        bg = TFT_RED;
        fg = TFT_WHITE;
        border = TFT_WHITE;
      }
    } else if (i < currentStepIdx) {
      bg = TFT_DARKGREEN;
      fg = TFT_WHITE;
      border = TFT_GREEN;
    } else if (i == currentStepIdx) {
      bg = TFT_CYAN;
      fg = TFT_BLACK;
      border = TFT_WHITE;
    }

    _statusSprite.fillRoundRect(sx, stepY, stepW, stepH, 6, bg);
    _statusSprite.drawRoundRect(sx, stepY, stepW, stepH, 6, border);
    _statusSprite.setTextSize(2);
    _statusSprite.setTextColor(fg, bg);
    _statusSprite.setCursor(sx + 10, stepY + 5);
    if (i < currentStepIdx && _stage != ConnStage::FEHLER) {
      _statusSprite.print("* ");
    }
    _statusSprite.print(steps[i]);
  }

  const int16_t infoY = stepY + stepH + 6;
  _statusSprite.setTextSize(2);
  _statusSprite.setTextColor(TFT_YELLOW, TFT_NAVY);
  _statusSprite.setCursor(kMargin + 4, infoY);
  _statusSprite.print("[");
  _statusSprite.print(_mode);
  _statusSprite.print("] ");
  _statusSprite.setTextColor(TFT_WHITE, TFT_NAVY);
  _statusSprite.print(_state);

  _statusSprite.pushSprite(0, kTabHeight);
}

// =============================================================================
// KACHEL 1: DREHZAHL ALS DREHINSTRUMENT (0 - 5000 RPM)
// =============================================================================
void Dashboard::drawGaugeRPM(LGFX_Sprite &s, int16_t w, int16_t h) {
  drawCardFrame(s, w, h, TFT_GREEN, "DREHZAHL", "0 - 5000 RPM");

  const int16_t cx = w / 2;
  const int16_t cy = h / 2 + 14;
  const int16_t r = 108;

  for (int16_t angle = 135; angle <= 405; angle += 2) {
    float rad = angle * (M_PI / 180.0f);
    int16_t x1 = cx + static_cast<int16_t>(cos(rad) * (r - 20));
    int16_t y1 = cy + static_cast<int16_t>(sin(rad) * (r - 20));
    int16_t x2 = cx + static_cast<int16_t>(cos(rad) * r);
    int16_t y2 = cy + static_cast<int16_t>(sin(rad) * r);
    uint16_t col = (angle > 351) ? TFT_RED : ((angle > 297) ? TFT_YELLOW : TFT_GREEN);
    s.drawLine(x1, y1, x2, y2, col);
  }

  for (int rpmMark = 0; rpmMark <= 5000; rpmMark += 1000) {
    float angle = 135.0f + (rpmMark / 5000.0f) * 270.0f;
    float rad = angle * (M_PI / 180.0f);
    int16_t tx = cx + static_cast<int16_t>(cos(rad) * (r - 34));
    int16_t ty = cy + static_cast<int16_t>(sin(rad) * (r - 34));
    s.setTextSize(2);
    s.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    s.setCursor(tx - 6, ty - 6);
    s.print(rpmMark / 1000);
  }

  float rpmClamped = _rpm > 5000 ? 5000 : _rpm;
  float needleAngle = 135.0f + (rpmClamped / 5000.0f) * 270.0f;
  float needleRad = needleAngle * (M_PI / 180.0f);
  int16_t nx = cx + static_cast<int16_t>(cos(needleRad) * (r - 20));
  int16_t ny = cy + static_cast<int16_t>(sin(needleRad) * (r - 20));

  s.drawLine(cx, cy, nx, ny, TFT_WHITE);
  s.drawLine(cx + 1, cy, nx + 1, ny, TFT_WHITE);
  s.drawLine(cx - 1, cy, nx - 1, ny, TFT_WHITE);
  s.drawLine(cx, cy + 1, nx, ny + 1, TFT_WHITE);
  s.drawLine(cx - 1, cy, nx - 1, ny, TFT_WHITE);
  s.fillCircle(cx, cy, 10, TFT_RED);

  s.setTextSize(4);
  s.setTextColor(TFT_WHITE, TFT_DARKGREY);
  char valBuf[16];
  snprintf(valBuf, sizeof(valBuf), "%u", _rpm);
  int16_t tw = strlen(valBuf) * 24;
  s.setCursor(cx - tw / 2, cy + 30);
  s.print(valBuf);

  s.setTextSize(2);
  s.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  s.setCursor(cx - 30, cy + 66);
  s.print("U/min");

  char foot[32];
  snprintf(foot, sizeof(foot), "Raw: %u | Max: 5000", _rpm > 32 ? (_rpm / 35 + 32) : 0);
  drawFooter(s, h, foot);
}

// =============================================================================
// KACHEL 2: MOTORSTATUS (ALLE STATUS ANZEIGEN & AKTIVEN MARKIEREN)
// =============================================================================
void Dashboard::drawMotorStatusMatrix(LGFX_Sprite &s, int16_t w, int16_t h) {
  uint16_t frameColor = _running ? TFT_GREEN : TFT_ORANGE;
  drawCardFrame(s, w, h, frameColor, "MOTORSTATUS", "Zustandsmatrix");

  const char *states[] = {
    "STOPP",
    "START",
    "LEERLAUF",
    "TEILLAST",
    "VOLLLAST",
    "SCHUB"
  };

  uint8_t activeIdx = 0;
  if (!_running) {
    activeIdx = 0;
  } else if (_rpm < 600) {
    activeIdx = 1;
  } else if (_g69Raw == 0 && _rpm >= 600 && _rpm <= 1100) {
    activeIdx = 2;
  } else if (_g69Raw > 40) {
    activeIdx = 4;
  } else if (_g69Raw > 0) {
    activeIdx = 3;
  } else if (_g69Raw == 0 && _rpm > 1300) {
    activeIdx = 5;
  } else {
    activeIdx = 2;
  }

  const int16_t rowStartX = 12;
  const int16_t rowStartY = 54;
  const int16_t itemH = 25;
  const int16_t itemW = w - 24;

  for (uint8_t i = 0; i < 6; ++i) {
    int16_t iy = rowStartY + i * (itemH + 2);
    bool isActive = (i == activeIdx);
    uint16_t bg = isActive ? (_running ? TFT_DARKGREEN : TFT_ORANGE) : TFT_BLACK;
    uint16_t fg = isActive ? TFT_WHITE : TFT_LIGHTGREY;
    uint16_t border = isActive ? TFT_WHITE : TFT_DARKGREY;

    s.fillRoundRect(rowStartX, iy, itemW, itemH, 4, bg);
    s.drawRoundRect(rowStartX, iy, itemW, itemH, 4, border);

    s.setTextSize(2);
    s.setTextColor(fg, bg);
    s.setCursor(rowStartX + 6, iy + 4);
    s.print(isActive ? ">> " : "   ");
    s.print(states[i]);
  }

  char foot[32];
  snprintf(foot, sizeof(foot), "Run: 0x%02X | Stat: 0x%02X", _runFlagRaw, _statusRaw);
  drawFooter(s, h, foot);
}

// =============================================================================
// KACHEL 3: BATTERIESPANNUNG ALS DREHINSTRUMENT (10 - 16V)
// =============================================================================
void Dashboard::drawGaugeBattery(LGFX_Sprite &s, int16_t w, int16_t h) {
  drawCardFrame(s, w, h, TFT_YELLOW, "BATTERIESPANNUNG", "10 - 16 Volt");

  const int16_t cx = w / 2;
  const int16_t cy = h / 2 + 14;
  const int16_t r = 108;

  for (int16_t angle = 135; angle <= 405; angle += 2) {
    float rad = angle * (M_PI / 180.0f);
    int16_t x1 = cx + static_cast<int16_t>(cos(rad) * (r - 20));
    int16_t y1 = cy + static_cast<int16_t>(sin(rad) * (r - 20));
    int16_t x2 = cx + static_cast<int16_t>(cos(rad) * r);
    int16_t y2 = cy + static_cast<int16_t>(sin(rad) * r);
    float vAtAngle = 10.0f + (angle - 135) * (6.0f / 270.0f);
    uint16_t col = (vAtAngle < 11.8f || vAtAngle > 15.0f) ? TFT_RED :
                   ((vAtAngle < 12.6f) ? TFT_YELLOW : TFT_GREEN);
    s.drawLine(x1, y1, x2, y2, col);
  }

  for (int vMark = 10; vMark <= 16; vMark += 1) {
    float angle = 135.0f + ((vMark - 10.0f) / 6.0f) * 270.0f;
    float rad = angle * (M_PI / 180.0f);
    int16_t tx = cx + static_cast<int16_t>(cos(rad) * (r - 34));
    int16_t ty = cy + static_cast<int16_t>(sin(rad) * (r - 34));
    s.setTextSize(2);
    s.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    s.setCursor(tx - 8, ty - 6);
    s.print(vMark);
  }

  float vClamped = _battery < 10.0f ? 10.0f : (_battery > 16.0f ? 16.0f : _battery);
  float needleAngle = 135.0f + ((vClamped - 10.0f) / 6.0f) * 270.0f;
  float needleRad = needleAngle * (M_PI / 180.0f);
  int16_t nx = cx + static_cast<int16_t>(cos(needleRad) * (r - 20));
  int16_t ny = cy + static_cast<int16_t>(sin(needleRad) * (r - 20));

  s.drawLine(cx, cy, nx, ny, TFT_WHITE);
  s.drawLine(cx + 1, cy, nx + 1, ny, TFT_WHITE);
  s.drawLine(cx - 1, cy, nx - 1, ny, TFT_WHITE);
  s.drawLine(cx, cy + 1, nx, ny + 1, TFT_WHITE);
  s.drawLine(cx - 1, cy, nx - 1, ny, TFT_WHITE);
  s.fillCircle(cx, cy, 10, TFT_YELLOW);

  s.setTextSize(4);
  s.setTextColor(TFT_WHITE, TFT_DARKGREY);
  char valBuf[16];
  snprintf(valBuf, sizeof(valBuf), "%.2f", _battery);
  int16_t tw = strlen(valBuf) * 24;
  s.setCursor(cx - tw / 2, cy + 30);
  s.print(valBuf);

  s.setTextSize(2);
  s.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  s.setCursor(cx - 24, cy + 66);
  s.print("Volt");

  uint8_t rawBat = static_cast<uint8_t>(_battery * 256.0f / 24.0f);
  char foot[32];
  snprintf(foot, sizeof(foot), "Raw: %u | Formel: 0x85", rawBat);
  drawFooter(s, h, foot);
}

// =============================================================================
// KACHEL 4: DROSSELKLAPPE G69 (VERTIKAL MITTIG AUSGERICHTET)
// =============================================================================
void Dashboard::drawThrottleValve(LGFX_Sprite &s, int16_t w, int16_t h) {
  drawCardFrame(s, w, h, TFT_CYAN, "DROSSELKLAPPE", "Geber G69");

  const int16_t pipeW = w - 40;
  const int16_t pipeH = 68;
  const int16_t pipeX = (w - pipeW) / 2;
  const int16_t pipeY = 56;
  const int16_t cx = pipeX + pipeW / 2;
  const int16_t cy = pipeY + pipeH / 2;

  s.fillRect(pipeX, pipeY, pipeW, pipeH, TFT_BLACK);
  s.fillRect(pipeX, pipeY, pipeW, 3, TFT_WHITE);
  s.fillRect(pipeX, pipeY + pipeH - 3, pipeW, 3, TFT_WHITE);
  s.drawFastVLine(pipeX, pipeY, pipeH, TFT_DARKGREY);
  s.drawFastVLine(pipeX + pipeW - 1, pipeY, pipeH, TFT_DARKGREY);

  s.drawLine(pipeX + 12, cy, pipeX + 28, cy, TFT_DARKGREY);
  s.drawLine(pipeX + 22, cy - 5, pipeX + 28, cy, TFT_DARKGREY);
  s.drawLine(pipeX + 22, cy + 5, pipeX + 28, cy, TFT_DARKGREY);

  float deg = rawToG69Deg(_g69Raw);
  float flapVisualAngleRad = (90.0f - deg) * (M_PI / 180.0f);
  const int16_t flapHalfLen = 30;

  int16_t fx1 = cx - static_cast<int16_t>(cos(flapVisualAngleRad) * flapHalfLen);
  int16_t fy1 = cy - static_cast<int16_t>(sin(flapVisualAngleRad) * flapHalfLen);
  int16_t fx2 = cx + static_cast<int16_t>(cos(flapVisualAngleRad) * flapHalfLen);
  int16_t fy2 = cy + static_cast<int16_t>(sin(flapVisualAngleRad) * flapHalfLen);

  s.drawLine(fx1, fy1, fx2, fy2, TFT_CYAN);
  s.drawLine(fx1 + 1, fy1, fx2 + 1, fy2, TFT_CYAN);
  s.drawLine(fx1 - 1, fy1, fx2 - 1, fy2, TFT_CYAN);
  s.drawLine(fx1, fy1 + 1, fx2, fy2 + 1, TFT_CYAN);
  s.drawLine(fx1 - 1, fy1, fx2 - 1, fy2, TFT_CYAN);
  s.fillCircle(cx, cy, 5, TFT_YELLOW);

  // Wert GROSS zentriert UNTER der Rohrdarstellung
  char degBuf[16];
  snprintf(degBuf, sizeof(degBuf), "%.1f", deg);
  s.setTextSize(4);
  s.setTextColor(TFT_WHITE, TFT_DARKGREY);
  int16_t valW = strlen(degBuf) * 24;
  int16_t totalBlockW = valW + 14 + 48; // Zahl + Symbol + "Grad"
  int16_t valX = (w - totalBlockW) / 2;
  int16_t valY = pipeY + pipeH + 12;
  s.setCursor(valX, valY);
  s.print(degBuf);

  int16_t degCircleX = valX + valW + 4;
  s.drawCircle(degCircleX + 4, valY + 4, 4, TFT_CYAN);
  s.drawCircle(degCircleX + 4, valY + 4, 3, TFT_CYAN);

  s.setTextSize(2);
  s.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  s.setCursor(degCircleX + 16, valY + 10);
  s.print("Grad");

  char foot[32];
  snprintf(foot, sizeof(foot), "Raw: %u | Grp 003 Z3", _g69Raw);
  drawFooter(s, h, foot);
}

// =============================================================================
// KACHEL 5: KUEHLMITTELTEMPERATUR (KOMPAKT, MITTIG AUSGERICHTET)
// =============================================================================
void Dashboard::drawThermometerCoolant(LGFX_Sprite &s, int16_t w, int16_t h) {
  drawCardFrame(s, w, h, TFT_SKYBLUE, "KUEHLMITTEL", "Motortemperatur");

  float tempC = rawToCoolantTemp(_coolantRaw);

  const int16_t tx = 28;
  const int16_t ty = 52;
  const int16_t th = 138;
  const int16_t tw = 18;

  s.fillRoundRect(tx, ty, tw, th, tw / 2, TFT_BLACK);
  s.drawRoundRect(tx, ty, tw, th, tw / 2, TFT_LIGHTGREY);
  s.fillCircle(tx + tw / 2, ty + th + 12, 16, TFT_BLACK);
  s.drawCircle(tx + tw / 2, ty + th + 12, 16, TFT_LIGHTGREY);

  float tempClamped = tempC < -20.0f ? -20.0f : (tempC > 120.0f ? 120.0f : tempC);
  float frac = (tempClamped + 20.0f) / 140.0f;
  int16_t fillH = static_cast<int16_t>(frac * (th - 6));
  uint16_t col = (tempC > 105.0f) ? TFT_RED : ((tempC > 70.0f) ? TFT_GREEN : TFT_BLUE);

  s.fillCircle(tx + tw / 2, ty + th + 12, 14, col);
  if (fillH > 0) {
    s.fillRoundRect(tx + 3, ty + th - fillH, tw - 6, fillH + 10, (tw - 6) / 2, col);
  }

  // Wert rechts vertikal mittig
  const int16_t rx = tx + tw + 20;
  s.setTextSize(4);
  s.setTextColor(TFT_WHITE, TFT_DARKGREY);
  s.setCursor(rx, 80);
  char tBuf[16];
  snprintf(tBuf, sizeof(tBuf), "%.1f", tempC);
  s.print(tBuf);

  int16_t degX = rx + strlen(tBuf) * 24 + 4;
  int16_t degY = 84;
  s.drawCircle(degX + 3, degY + 3, 3, TFT_SKYBLUE);
  s.drawCircle(degX + 3, degY + 3, 2, TFT_SKYBLUE);
  s.setTextSize(3);
  s.setTextColor(TFT_SKYBLUE, TFT_DARKGREY);
  s.setCursor(degX + 12, 84);
  s.print("C");

  s.setTextSize(2);
  s.setCursor(rx, 134);
  if (tempC >= 80.0f && tempC <= 100.0f) {
    s.setTextColor(TFT_GREEN, TFT_DARKGREY);
    s.print("BETRIEB OK");
  } else if (tempC > 105.0f) {
    s.setTextColor(TFT_RED, TFT_DARKGREY);
    s.print("HEISS");
  } else {
    s.setTextColor(TFT_BLUE, TFT_DARKGREY);
    s.print("WARMLAUF");
  }

  char foot[32];
  snprintf(foot, sizeof(foot), "Raw: %u | Formel 0x8C", _coolantRaw);
  drawFooter(s, h, foot);
}

// =============================================================================
// KACHEL 6: ANSAUGLUFTTEMPERATUR (IAT) (KOMPAKT, MITTIG AUSGERICHTET)
// =============================================================================
void Dashboard::drawThermometerIAT(LGFX_Sprite &s, int16_t w, int16_t h) {
  drawCardFrame(s, w, h, TFT_SKYBLUE, "ANSAUGLUFT", "Geber G42");

  float tempC = rawToIatTemp(_iatRaw);

  const int16_t tx = 28;
  const int16_t ty = 52;
  const int16_t th = 138;
  const int16_t tw = 18;

  s.fillRoundRect(tx, ty, tw, th, tw / 2, TFT_BLACK);
  s.drawRoundRect(tx, ty, tw, th, tw / 2, TFT_LIGHTGREY);
  s.fillCircle(tx + tw / 2, ty + th + 12, 16, TFT_BLACK);
  s.drawCircle(tx + tw / 2, ty + th + 12, 16, TFT_LIGHTGREY);

  float tempClamped = tempC < -20.0f ? -20.0f : (tempC > 100.0f ? 100.0f : tempC);
  float frac = (tempClamped + 20.0f) / 120.0f;
  int16_t fillH = static_cast<int16_t>(frac * (th - 6));
  uint16_t col = (tempC > 60.0f) ? TFT_ORANGE : TFT_CYAN;

  s.fillCircle(tx + tw / 2, ty + th + 12, 14, col);
  if (fillH > 0) {
    s.fillRoundRect(tx + 3, ty + th - fillH, tw - 6, fillH + 10, (tw - 6) / 2, col);
  }

  const int16_t rx = tx + tw + 20;
  s.setTextSize(4);
  s.setTextColor(TFT_WHITE, TFT_DARKGREY);
  s.setCursor(rx, 80);
  char tBuf[16];
  snprintf(tBuf, sizeof(tBuf), "%.1f", tempC);
  s.print(tBuf);

  int16_t degX = rx + strlen(tBuf) * 24 + 4;
  int16_t degY = 84;
  s.drawCircle(degX + 3, degY + 3, 3, TFT_SKYBLUE);
  s.drawCircle(degX + 3, degY + 3, 2, TFT_SKYBLUE);
  s.setTextSize(3);
  s.setTextColor(TFT_SKYBLUE, TFT_DARKGREY);
  s.setCursor(degX + 12, 84);
  s.print("C");

  s.setTextSize(2);
  s.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  s.setCursor(rx, 134);
  s.print("NORMAL");

  char foot[32];
  snprintf(foot, sizeof(foot), "Raw: %u | Formel 0x8C", _iatRaw);
  drawFooter(s, h, foot);
}

// =============================================================================
// KACHEL 7: LAMBDA-REGELUNG (MITTIG AUSGERICHTET)
// =============================================================================
void Dashboard::drawLambdaCard(LGFX_Sprite &s, int16_t w, int16_t h) {
  // Lambda 128 = 1.000. Rechnerischer Lambda-Faktor ca. raw / 128.0f
  float lambdaEst = _lambdaRaw > 0 ? (_lambdaRaw / 128.0f) : 1.0f;
  bool isLambdaOk = (_lambdaRaw >= 115 && _lambdaRaw <= 145);
  uint16_t cardCol = isLambdaOk ? TFT_GREEN : (_lambdaRaw < 115 ? TFT_CYAN : TFT_ORANGE);

  drawCardFrame(s, w, h, cardCol, "LAMBDA (O2S)", "Regelwert");

  // Grosser Lambda-Wert vertikal mittig
  s.setTextSize(4);
  s.setTextColor(TFT_WHITE, TFT_DARKGREY);
  char lBuf[16];
  snprintf(lBuf, sizeof(lBuf), "%.3f", lambdaEst);
  int16_t lw = strlen(lBuf) * 24;
  int16_t lx = (w - lw) / 2;
  s.setCursor(lx, 66);
  s.print(lBuf);

  // Horizontaler Regelbalken (Mitte 128)
  const int16_t barW = w - 40;
  const int16_t barX = (w - barW) / 2;
  const int16_t barY = 118;
  const int16_t barH = 16;

  s.fillRoundRect(barX, barY, barW, barH, 4, TFT_BLACK);
  s.drawRoundRect(barX, barY, barW, barH, 4, TFT_LIGHTGREY);
  // Markierung Mitte (128)
  s.drawFastVLine(barX + barW / 2, barY - 2, barH + 4, TFT_WHITE);

  // Positionsanzeiger
  int16_t pos = barX + static_cast<int16_t>((static_cast<float>(_lambdaRaw) / 255.0f) * barW);
  if (pos < barX + 3) pos = barX + 3;
  if (pos > barX + barW - 5) pos = barX + barW - 5;
  s.fillRoundRect(pos - 3, barY - 3, 6, barH + 6, 2, cardCol);

  s.setTextSize(2);
  const char *statText = "LAMBDA = 1.0 (OK)";
  if (_lambdaRaw < 115) {
    s.setTextColor(TFT_CYAN, TFT_DARKGREY);
    statText = "MAGER (FETT REG.)";
  } else if (_lambdaRaw > 145) {
    s.setTextColor(TFT_ORANGE, TFT_DARKGREY);
    statText = "FETT (MAGER REG.)";
  } else {
    s.setTextColor(TFT_GREEN, TFT_DARKGREY);
  }
  int16_t txtW = strlen(statText) * 12;
  s.setCursor((w - txtW) / 2, 146);
  s.print(statText);

  char foot[32];
  snprintf(foot, sizeof(foot), "Raw: %u | Mitte: 128", _lambdaRaw);
  drawFooter(s, h, foot);
}

// =============================================================================
// KACHEL 8: EINSPRITZZEIT / LAST (ti) (MITTIG AUSGERICHTET)
// =============================================================================
void Dashboard::drawInjectionCard(LGFX_Sprite &s, int16_t w, int16_t h) {
  drawCardFrame(s, w, h, TFT_MAGENTA, "EINSPRITZUNG", "Grundeinspritzzeit ti");

  // Große Darstellung von InjRaw
  char iBuf[16];
  snprintf(iBuf, sizeof(iBuf), "%u", _injRaw);
  int16_t iw = strlen(iBuf) * 30;
  int16_t totalW = iw + 8 + 36;
  int16_t ix = (w - totalW) / 2;

  s.setTextSize(5);
  s.setTextColor(TFT_WHITE, TFT_DARKGREY);
  s.setCursor(ix, 60);
  s.print(iBuf);

  s.setTextSize(3);
  s.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  s.setCursor(ix + iw + 8, 74);
  s.print("raw");

  // Dynamischer Lastbalken
  const int16_t barW = w - 40;
  const int16_t barX = (w - barW) / 2;
  const int16_t barY = 118;
  const int16_t barH = 16;

  s.fillRoundRect(barX, barY, barW, barH, 4, TFT_BLACK);
  s.drawRoundRect(barX, barY, barW, barH, 4, TFT_LIGHTGREY);

  float loadFrac = static_cast<float>(_injRaw) / 30.0f;
  if (loadFrac > 1.0f) loadFrac = 1.0f;
  int16_t fillW = static_cast<int16_t>(loadFrac * (barW - 4));
  if (fillW > 0) {
    uint16_t col = (_injRaw > 15) ? TFT_RED : ((_injRaw > 8) ? TFT_YELLOW : TFT_MAGENTA);
    s.fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 2, col);
  }

  s.setTextSize(2);
  const char *loadText = "LEERLAUFLAST";
  if (!_running || _injRaw == 0) {
    s.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    loadText = "SCHUB / AUS";
  } else if (_injRaw <= 6) {
    s.setTextColor(TFT_GREEN, TFT_DARKGREY);
    loadText = "LEERLAUFLAST";
  } else {
    s.setTextColor(TFT_YELLOW, TFT_DARKGREY);
    loadText = "TEILLAST / BESCHL.";
  }
  int16_t loadTxtW = strlen(loadText) * 12;
  s.setCursor((w - loadTxtW) / 2, 146);
  s.print(loadText);

  char foot[32];
  snprintf(foot, sizeof(foot), "Raw: %u | Grp 000 F9", _injRaw);
  drawFooter(s, h, foot);
}

// =============================================================================
// TAB 1: GESAMT-WERTEANSICHT (8 KACHELN: 4 SPALTEN x 2 ZEILEN)
// =============================================================================
void Dashboard::drawValues() {
  const int16_t gap = 12;
  const int16_t cols = 4;
  const int16_t w = (M5.Display.width() - 2 * kMargin - (cols - 1) * gap) / cols;
  const int16_t h = (M5.Display.height() - kContentTop - kMargin - gap) / 2;
  const int16_t row1Y = kContentTop + 8;
  const int16_t row2Y = row1Y + h + gap;

  auto colX = [&](int16_t col) { return static_cast<int16_t>(kMargin + col * (w + gap)); };

  // Zeile 1:
  // Kachel 1 (Col 0): MOTORSTATUS (oben links!)
  drawMotorStatusMatrix(_tileSprite, w, h);
  _tileSprite.pushSprite(colX(0), row1Y);

  // Kachel 2 (Col 1): DREHZAHL (getauscht mit Motorstatus)
  drawGaugeRPM(_tileSprite, w, h);
  _tileSprite.pushSprite(colX(1), row1Y);

  // Kachel 3 (Col 2): BATTERIE
  drawGaugeBattery(_tileSprite, w, h);
  _tileSprite.pushSprite(colX(2), row1Y);

  // Kachel 4 (Col 3): DROSSELKLAPPE
  drawThrottleValve(_tileSprite, w, h);
  _tileSprite.pushSprite(colX(3), row1Y);

  // Zeile 2:
  // Kachel 5 (Col 0): KUEHLMITTEL
  drawThermometerCoolant(_tileSprite, w, h);
  _tileSprite.pushSprite(colX(0), row2Y);

  // Kachel 6 (Col 1): ANSAUGLUFT
  drawThermometerIAT(_tileSprite, w, h);
  _tileSprite.pushSprite(colX(1), row2Y);

  // Kachel 7 (Col 2): LAMBDA
  drawLambdaCard(_tileSprite, w, h);
  _tileSprite.pushSprite(colX(2), row2Y);

  // Kachel 8 (Col 3): EINSPRITZUNG
  drawInjectionCard(_tileSprite, w, h);
  _tileSprite.pushSprite(colX(3), row2Y);
}

// =============================================================================
// TAB 2: ECU-INFORMATION & LIVE-LOG (FLACKERFREI UEBER SPRITES)
// =============================================================================
void Dashboard::drawInfo() {
  const int16_t gap = 16;
  const int16_t topH = 210;
  const int16_t cardW = (M5.Display.width() - 2 * kMargin - gap) / 2;
  const int16_t yTop = kContentTop + 8;

  // Obere linke Kachel: Steuergeraet & Identifikation (flackerfrei ueber _tab2CardSprite)
  {
    drawCardFrame(_tab2CardSprite, cardW, topH, TFT_CYAN, "STEUERGERAET & SYSTEM", "Digifant 1.7");
    _tab2CardSprite.setTextSize(2);
    _tab2CardSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    _tab2CardSprite.setCursor(16, 62);
    _tab2CardSprite.print("Ident: ");
    _tab2CardSprite.setTextColor(TFT_YELLOW, TFT_DARKGREY);
    String shortId = _ecuInfo;
    if (shortId.length() > 28) shortId = shortId.substring(0, 26) + "..";
    _tab2CardSprite.println(shortId);

    _tab2CardSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    _tab2CardSprite.setCursor(16, 96);
    _tab2CardSprite.print("Motor: ");
    _tab2CardSprite.println("VW 2E (2.0L 8V)");

    _tab2CardSprite.setCursor(16, 130);
    _tab2CardSprite.print("Protokoll: ");
    _tab2CardSprite.println("KWP1281 (1200 Baud)");

    _tab2CardSprite.setCursor(16, 164);
    _tab2CardSprite.print("Gruppen: ");
    _tab2CardSprite.println("000 (0x12), 001..004");
    _tab2CardSprite.pushSprite(kMargin, yTop);
  }

  // Obere rechte Kachel: Diagnose-Status & Statistik (flackerfrei ueber _tab2CardSprite)
  {
    const int16_t x = kMargin + cardW + gap;
    drawCardFrame(_tab2CardSprite, cardW, topH, TFT_GREEN, "DIAGNOSE & VERBINDUNG", "Hardware-Link");
    _tab2CardSprite.setTextSize(2);
    _tab2CardSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    _tab2CardSprite.setCursor(16, 62);
    _tab2CardSprite.print("Modus: ");
    _tab2CardSprite.setTextColor(TFT_CYAN, TFT_DARKGREY);
    _tab2CardSprite.println(_mode);

    _tab2CardSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
    _tab2CardSprite.setCursor(16, 96);
    _tab2CardSprite.print("Status: ");
    _tab2CardSprite.println(_state);

    _tab2CardSprite.setCursor(16, 130);
    _tab2CardSprite.print("RX-Bloecke: ");
    _tab2CardSprite.println(_blockCount);

    _tab2CardSprite.setCursor(16, 164);
    _tab2CardSprite.print("Transport: ");
    _tab2CardSprite.println(_mode.indexOf("SIMULATION") >= 0 ? "Autonom (108 Bloecke)" : "AutoDia K409 (FTDI)");
    _tab2CardSprite.pushSprite(x, yTop);
  }

  // Untere Haelfte: Live-Konsolenausgabe (Log-Terminal, 100% flackerfrei ueber _logSprite)
  const int16_t logY = yTop + topH + 12;
  const int16_t logH = M5.Display.height() - logY - kMargin;
  const int16_t logW = M5.Display.width() - 2 * kMargin;

  _logSprite.fillScreen(TFT_BLACK);
  _logSprite.fillRoundRect(0, 0, logW, logH, 8, TFT_BLACK);
  _logSprite.drawRoundRect(0, 0, logW, logH, 8, TFT_DARKCYAN);

  _logSprite.setTextSize(2);
  _logSprite.setTextColor(TFT_CYAN, TFT_BLACK);
  _logSprite.setCursor(12, 8);
  _logSprite.print("LIVE-KONSOLENAUSGABE (LOG):");

  const uint8_t lineCount = console.getLineCount();
  const int16_t fontH = 20;
  const int16_t maxLines = (logH - 36) / fontH;
  const uint8_t start = lineCount > maxLines ? (lineCount - maxLines) : 0;

  _logSprite.setTextSize(2);
  _logSprite.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  for (uint8_t i = start; i < lineCount; ++i) {
    const int16_t lineY = 34 + (i - start) * fontH;
    _logSprite.setCursor(14, lineY);
    String line = console.getLine(i);
    if (line.length() > 95) {
      line = line.substring(0, 92) + "...";
    }
    _logSprite.print(line);
  }

  _logSprite.pushSprite(kMargin, logY);
}

// =============================================================================
// TAB 3: MESSSCHRIEB (FLACKERFREI UEBER _scopeSprite)
// =============================================================================
void Dashboard::drawScope() {
  const int16_t y0 = kContentTop + 8;
  const int16_t scopeW = M5.Display.width() - 2 * kMargin;
  const int16_t scopeH = M5.Display.height() - y0 - kMargin;

  _scopeSprite.fillScreen(TFT_BLACK);
  _scopeSprite.fillRoundRect(0, 0, scopeW, scopeH, 12, TFT_DARKGREY);
  _scopeSprite.drawRoundRect(0, 0, scopeW, scopeH, 12, TFT_MAGENTA);

  _scopeSprite.setTextSize(3);
  _scopeSprite.setTextColor(TFT_WHITE, TFT_DARKGREY);
  _scopeSprite.setCursor(24, 40);
  _scopeSprite.println("Vorbereitung fuer Meilenstein M6 (Testfahrt)");

  _scopeSprite.setTextSize(2);
  _scopeSprite.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
  _scopeSprite.setCursor(24, 90);
  _scopeSprite.println("- Drehzahlverlauf (RPM) live grafisch zeichnen");
  _scopeSprite.setCursor(24, 120);
  _scopeSprite.println("- G69 Drosselklappenstellung in Grad ueber Zeit");
  _scopeSprite.setCursor(24, 150);
  _scopeSprite.println("- Leerlauf- und Schubabschaltungsphasen farbig markieren");
  _scopeSprite.setCursor(24, 180);
  _scopeSprite.println("- Korrelation Bergabfahrt / Ruckeln / Schubbetrieb");
  _scopeSprite.setCursor(24, 220);
  _scopeSprite.setTextColor(TFT_CYAN, TFT_DARKGREY);
  _scopeSprite.println("Graph wird bei Beginn der Testfahrt automatisch aktiviert.");

  _scopeSprite.pushSprite(kMargin, y0);
}

void Dashboard::selectTab(uint8_t tab) {
  uint8_t newTab = tab % 3;
  if (newTab != _tab) {
    _tab = newTab;
    _needFullClear = true;
  }
  _dirty = true;
}
