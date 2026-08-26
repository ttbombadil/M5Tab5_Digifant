#pragma once

#include <M5Unified.h>

#include <cstddef>
#include <cstdint>

#include "display_ui_model.h"
#include "logger_channels.h"
#include "runtime_debug.h"

namespace digifant::ui {

// Target-only display consumer. Rendering follows the proven prototype:
// small reusable PSRAM sprites, never a full-screen sprite.
class DisplayUi {
 public:
  void bindLogger(logging::LoggerCommandQueue& commands) noexcept;
  void bindRuntimeDebug(runtime::RuntimeDebug* debug) noexcept;
  void consumeLoggerStatus(const logging::LoggerStatus& status) noexcept;
  void begin();
  void consume(const MeasurementSnapshot& snapshot) noexcept;
  void setTabFromSerial(uint8_t tab, uint32_t requestSequence = 0) noexcept;
  void update();
  const DisplayUiModel& model() const noexcept;

 private:
  static constexpr uint16_t kBlack = 0x0000;
  static constexpr uint16_t kPanel = 0x10A2;
  static constexpr uint16_t kPanel2 = 0x2965;
  static constexpr uint16_t kBlue = 0x2BCF;
  static constexpr uint16_t kGreen = 0x072E;
  static constexpr uint16_t kAmber = 0xFD60;
  static constexpr uint16_t kGrey = 0x8410;
  static constexpr uint16_t kRed = 0xF8A4;
  static constexpr int16_t kMargin = 12;
  static constexpr int16_t kGap = 12;
  static constexpr int16_t kTabHeight = 52;
  static constexpr int16_t kStatusHeight = 58;
  static constexpr int16_t kContentTop = kTabHeight + kStatusHeight;
  static constexpr int16_t kRowHeight = 42;
  static constexpr int16_t kScopeHeight = 430;
  static constexpr uint32_t kRefreshMs = 150;
  static constexpr uint32_t kSpriteInitRetryMs = 500;
  static constexpr uint8_t kMaxSpriteInitAttempts = 20;

  bool initializeSprites();
  void handleTouch();
  void draw();
  void drawTabs();
  void drawStatus();
  void statusItem(int16_t x, const char* label, bool ok);
  const MeasurementField* field(uint8_t group, uint8_t zone) const noexcept;
  static const char* semanticName(FieldSemantic semantic) noexcept;
  static const char* unitName(FieldUnit unit) noexcept;
  static uint16_t valueColor(const MeasurementField& value,
                             const MeasurementSnapshot& snapshot) noexcept;
  static const char* evidenceName(EvidenceGrade evidence) noexcept;
  static void valueText(const MeasurementField& value, const MeasurementSnapshot& snapshot,
                        char* out, std::size_t size) noexcept;
  static void compactValueText(const MeasurementField& value,
                               const MeasurementSnapshot& snapshot, char* out,
                               std::size_t size) noexcept;
  void drawTile(const char* label, const MeasurementField* value, uint16_t fallbackRaw,
                int16_t x, int16_t y, int16_t w, int16_t h, bool allowFallback = true,
                bool requiresRunningEngine = false);
  void drawCompact();
  void drawList();
  void drawSystem();
  static const char* validityName(SignalValidity value) noexcept;
  void drawTraces();
  void sendLoggerCommand(logging::LoggerCommandKind kind) noexcept;
  const char* loggerShortStatus() const noexcept;
  uint16_t loggerColor() const noexcept;
  void drawLoggerControls();
  void drawTraceLine(const DisplayScopeRing<DisplayUiModel::kScopeCapacity>& ring,
                     uint8_t signal, uint16_t color);
  static int16_t traceY(const DisplayScopeSample& sample, uint8_t signal,
                        int16_t height) noexcept;

  static constexpr int16_t kLoggerButtonY = 600;
  static constexpr int16_t kLoggerButtonH = 92;
  static constexpr int16_t kLogButtonX = 560;
  static constexpr int16_t kLogButtonW = 230;
  static constexpr int16_t kSprotzButtonX = 800;
  static constexpr int16_t kSprotzButtonW = 270;
  static constexpr int16_t kMarkerButtonX = 1085;
  static constexpr int16_t kMarkerButtonW = 180;

  LGFX_Sprite tileSprite_{&M5.Display};
  LGFX_Sprite statusSprite_{&M5.Display};
  LGFX_Sprite rowSprite_{&M5.Display};
  LGFX_Sprite scopeSprite_{&M5.Display};
  DisplayUiModel model_{};
  logging::LoggerCommandQueue* loggerCommands_ = nullptr;
  runtime::RuntimeDebug* runtimeDebug_ = nullptr;
  logging::LoggerStatus loggerStatus_{};
  DisplayTab lastDrawnTab_ = static_cast<DisplayTab>(255);
  int16_t width_ = 0;
  int16_t height_ = 0;
  uint32_t lastDrawMs_ = 0;
  uint32_t lastSpriteInitMs_ = 0;
  uint8_t spriteInitAttempts_ = 0;
  uint32_t appliedTabSequence_ = 0;
  bool ready_ = false;
  bool needLayout_ = true;
  bool loggerDirty_ = true;
  bool needLoggerControls_ = true;
  bool loggerCommandRejected_ = false;
};

}  // namespace digifant::ui
