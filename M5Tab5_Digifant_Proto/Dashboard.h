#pragma once

#include <Arduino.h>
#include <M5Unified.h>

enum class ConnStage : uint8_t {
  BEREIT = 0,
  BUS_INIT = 1,
  HANDSHAKE = 2,
  IDENTIFIKATION = 3,
  VERBUNDEN = 4,
  FEHLER = 5
};

// Vollflaechiges, flackerfreies Touch-Dashboard fuer das M5Tab5 (1280x720 Querformat).
// - Tab 1: 6 vollwertige Instrumentenkacheln (Drehzahl-Rundinstrument 0-5000 RPM,
//          Motorstatus-Zustandsmatrix, Batterie-Rundinstrument 0-15V,
//          Drosselklappe als schematische Klappe in Grad,
//          Kuehlmittel-Thermometer in °C, Ansaugluft-Thermometer in °C)
// - Tab 2: System-/ECU-Info oben, Live-Konsolen-Log unten
// - Tab 3: Vorbereiteter Messschrieb (Historien-Graph)
class Dashboard {
public:
  void begin();
  void update();

  void setGroup000(uint16_t rpm, uint8_t coolantRaw, uint8_t iatRaw,
                   uint8_t statusRaw, uint8_t runFlagRaw,
                   uint8_t lambdaRaw = 128, uint8_t injRaw = 0);
  void setBattery(float volts);
  void setG69(uint8_t raw);
  void setLambda(uint8_t raw);
  void setInjTime(uint8_t raw);
  void setEcuInfo(const String &info);

  void setMode(const String &mode);
  void setState(const String &state);
  void setStage(ConnStage stage, const String &detail = "");
  void incBlockCount() { _blockCount++; _dirty = true; }
  void selectTab(uint8_t tab);
  uint8_t getActiveTab() const { return _tab; }

  static float rawToCoolantTemp(uint8_t raw);
  static float rawToIatTemp(uint8_t raw);
  static float rawToG69Deg(uint8_t raw);

private:
  void draw();
  void drawValues();
  void drawInfo();
  void drawScope();
  void drawTabs();
  void drawStatusBar();
  void handleTouch();

  // Instrument-Renderfunktionen (8 Kacheln: 4 Spalten x 2 Zeilen)
  void drawGaugeRPM(LGFX_Sprite &s, int16_t w, int16_t h);
  void drawMotorStatusMatrix(LGFX_Sprite &s, int16_t w, int16_t h);
  void drawGaugeBattery(LGFX_Sprite &s, int16_t w, int16_t h);
  void drawThrottleValve(LGFX_Sprite &s, int16_t w, int16_t h);
  void drawThermometerCoolant(LGFX_Sprite &s, int16_t w, int16_t h);
  void drawThermometerIAT(LGFX_Sprite &s, int16_t w, int16_t h);
  void drawLambdaCard(LGFX_Sprite &s, int16_t w, int16_t h);
  void drawInjectionCard(LGFX_Sprite &s, int16_t w, int16_t h);

  static constexpr size_t kScopeHistoryLen = 240;

  struct ScopeSample {
    uint16_t rpm;
    uint8_t g69;
    uint8_t lambda;
    uint8_t inj;
    uint8_t status;
    bool running;
  };

  void pushScopeSample();

  LGFX_Sprite _tileSprite{&M5.Display};
  LGFX_Sprite _statusSprite{&M5.Display};
  LGFX_Sprite _tab2CardSprite{&M5.Display};
  LGFX_Sprite _logSprite{&M5.Display};
  LGFX_Sprite _scopeSprite{&M5.Display};
  bool _spritesCreated = false;

  ScopeSample _history[kScopeHistoryLen] = {};
  size_t _historyHead = 0;
  size_t _historyCount = 0;
  uint32_t _lastSampleMs = 0;
  bool _scopePaused = false;

  uint8_t _tab = 0;
  uint8_t _lastDrawnTab = 255;
  uint32_t _lastDrawMs = 0;
  bool _dirty = true;
  bool _needFullClear = true;
  bool _running = false;
  uint16_t _rpm = 0;
  uint8_t _coolantRaw = 9;
  uint8_t _iatRaw = 55;
  uint8_t _statusRaw = 0;
  uint8_t _runFlagRaw = 0x00;
  uint8_t _g69Raw = 0;
  uint8_t _lambdaRaw = 128;
  uint8_t _injRaw = 4;
  float _battery = 13.7f;
  uint32_t _blockCount = 0;
  String _ecuInfo = "037906024AG DIGIFANT 1.7 1576";
  String _mode = "SIMULATION";
  String _state = "Bereit";
  ConnStage _stage = ConnStage::BEREIT;
};

extern Dashboard dashboard;
