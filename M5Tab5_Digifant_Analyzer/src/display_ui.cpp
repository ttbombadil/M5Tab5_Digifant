#include "display_ui.h"

#include <Arduino.h>
#include <esp_timer.h>

#include <cstdio>

namespace digifant::ui {

void DisplayUi::bindLogger(logging::LoggerCommandQueue& commands) noexcept { loggerCommands_ = &commands; }

void DisplayUi::consumeLoggerStatus(const logging::LoggerStatus& status) noexcept {
  if (status.state == loggerStatus_.state && status.lastError == loggerStatus_.lastError &&
      status.storagePresent == loggerStatus_.storagePresent &&
      status.snapshotsWritten == loggerStatus_.snapshotsWritten &&
      status.eventsWritten == loggerStatus_.eventsWritten &&
      status.queueDrops == loggerStatus_.queueDrops) return;
  loggerStatus_ = status;
  loggerDirty_ = true;
  needLoggerControls_ = true;
}

void DisplayUi::begin() {
  if (M5.Display.height() > M5.Display.width())
    M5.Display.setRotation((M5.Display.getRotation() + 1) & 3);
  M5.Display.fillScreen(kBlack);
  spriteInitAttempts_ = 0;
  (void)initializeSprites();
}

void DisplayUi::consume(const MeasurementSnapshot& snapshot) noexcept { model_.accept(snapshot); }

void DisplayUi::setTabFromSerial(uint8_t tab) noexcept {
  if (tab >= 4U) return;
  model_.setTab(static_cast<DisplayTab>(tab));
  needLayout_ = true;
  loggerDirty_ = true;
}

void DisplayUi::update() {
  M5.update();
  if (!ready_) {
    const uint32_t now = millis();
    if (spriteInitAttempts_ < kMaxSpriteInitAttempts &&
        now - lastSpriteInitMs_ >= kSpriteInitRetryMs) {
      (void)initializeSprites();
    }
    return;
  }
  handleTouch();
  model_.sampleIfDue(millis());
  const uint32_t now = millis();
  if ((!model_.dirty() && !loggerDirty_) || now - lastDrawMs_ < kRefreshMs) return;
  draw();
}

const DisplayUiModel& DisplayUi::model() const noexcept { return model_; }

bool DisplayUi::initializeSprites() {
  ++spriteInitAttempts_;
  lastSpriteInitMs_ = millis();
  tileSprite_.deleteSprite();
  statusSprite_.deleteSprite();
  rowSprite_.deleteSprite();
  scopeSprite_.deleteSprite();

  width_ = M5.Display.width();
  height_ = M5.Display.height();
  if (width_ <= 2 * kMargin + 3 * kGap || height_ <= kContentTop + kMargin + kGap) {
    ready_ = false;
  } else {
    const int16_t tileW = (width_ - 2 * kMargin - 3 * kGap) / 4;
    const int16_t tileH = (height_ - kContentTop - kMargin - kGap) / 2;
    tileSprite_.setPsram(true);
    tileSprite_.createSprite(tileW, tileH);
    statusSprite_.setPsram(true);
    statusSprite_.createSprite(width_, kStatusHeight);
    rowSprite_.setPsram(true);
    rowSprite_.createSprite(width_ - 2 * kMargin, kRowHeight);
    scopeSprite_.setPsram(true);
    scopeSprite_.createSprite(width_ - 2 * kMargin, kScopeHeight);
    ready_ = tileSprite_.width() == tileW && tileSprite_.height() == tileH &&
             statusSprite_.width() == width_ && rowSprite_.width() == width_ - 2 * kMargin &&
             scopeSprite_.width() == width_ - 2 * kMargin;
  }

  if (!ready_) {
    if (width_ > 0 && height_ > 0) {
      M5.Display.setTextColor(TFT_WHITE, kBlack);
      M5.Display.setTextSize(2);
      M5.Display.drawString("Display wird initialisiert...", 24, 32);
    }
    return false;
  }
  needLayout_ = true;
  lastDrawnTab_ = static_cast<DisplayTab>(255);
  draw();
  return true;
}

void DisplayUi::handleTouch() {
  if (M5.Touch.getCount() == 0) return;
  const auto& touch = M5.Touch.getDetail(0);
  if (!touch.wasPressed()) return;
  if (touch.y < kTabHeight) {
    const DisplayTab old = model_.tab();
    model_.setTab(static_cast<DisplayTab>(touch.x / (width_ / 4)));
    if (old != model_.tab()) needLayout_ = true;
    return;
  }
  if (model_.tab() == DisplayTab::List && touch.y >= kContentTop) {
    const uint8_t page = touch.x < width_ / 2 ? 0 : 1;
    if (page != model_.listPage()) needLayout_ = true;
    model_.setListPage(page);
  } else if (model_.tab() == DisplayTab::Traces && touch.y >= kContentTop) {
    if (touch.y >= kLoggerButtonY) {
      if (touch.x >= kStartButtonX && touch.x < kStartButtonX + kStartButtonW) {
        const auto kind = loggerStatus_.state == logging::LoggerState::Recording
                              ? logging::LoggerCommandKind::Stop
                              : logging::LoggerCommandKind::Start;
        sendLoggerCommand(kind);
      } else if (touch.x >= kMarkerButtonX &&
                 loggerStatus_.state == logging::LoggerState::Recording) {
        sendLoggerCommand(logging::LoggerCommandKind::Marker);
      }
    } else {
      model_.toggleScopePause();
    }
  }
}

void DisplayUi::draw() {
  lastDrawMs_ = millis();
  M5.Display.startWrite();
  if (needLayout_ || lastDrawnTab_ != model_.tab()) {
    M5.Display.fillScreen(kBlack);
    drawTabs();
    lastDrawnTab_ = model_.tab();
    needLayout_ = false;
    needLoggerControls_ = true;
  }
  drawStatus();
  switch (model_.tab()) {
    case DisplayTab::Compact: drawCompact(); break;
    case DisplayTab::List: drawList(); break;
    case DisplayTab::System: drawSystem(); break;
    case DisplayTab::Traces: drawTraces(); break;
  }
  M5.Display.endWrite();
  model_.clearDirty();
  loggerDirty_ = false;
}

void DisplayUi::drawTabs() {
  static constexpr const char* names[] = {"KOMPAKT", "LISTE", "SYSTEM", "MITSCHRIEBE"};
  const int16_t tabW = width_ / 4;
  for (uint8_t i = 0; i < 4; ++i) {
    const uint16_t bg = static_cast<uint8_t>(model_.tab()) == i ? kBlue : kPanel2;
    M5.Display.fillRect(i * tabW, 0, tabW - 2, kTabHeight, bg);
    M5.Display.setTextColor(TFT_WHITE, bg);
    M5.Display.setTextSize(i == 3 ? 2 : 3);
    M5.Display.setCursor(i * tabW + 20, i == 3 ? 17 : 12);
    M5.Display.print(names[i]);
  }
}

void DisplayUi::drawStatus() {
  const auto& s = model_.snapshot();
  statusSprite_.fillScreen(kBlack);
  statusSprite_.setTextSize(2);
  statusItem(12, "K409", s.k409Connected);
  statusItem(140, "KWP", s.kwpConnected);
  statusItem(260, "ECU", s.ecuDataValid && s.validity == SignalValidity::Valid);
  char text[30]{};
  statusSprite_.setTextColor(TFT_WHITE, kBlack);
  snprintf(text, sizeof(text), "Frames %lu", static_cast<unsigned long>(s.frameCount));
  statusSprite_.drawString(text, 390, 18);
  const uint32_t drops = s.frameDrops + s.rxIngressDrops + s.snapshotOverwrites;
  snprintf(text, sizeof(text), "Drops %lu", static_cast<unsigned long>(drops));
  statusSprite_.drawString(text, 570, 18);
  const uint32_t faults = s.faultCount + s.parserRejects + s.actionFailures +
                          (s.byteFault == 0 ? 0U : 1U);
  snprintf(text, sizeof(text), "Faults %lu", static_cast<unsigned long>(faults));
  statusSprite_.drawString(text, 730, 18);
  snprintf(text, sizeof(text), "Gen %lu / S %lu", static_cast<unsigned long>(s.transportGeneration),
           static_cast<unsigned long>(s.sessionEpoch));
  statusSprite_.setTextColor(kGrey, kBlack);
  statusSprite_.drawString(text, 900, 18);
  statusSprite_.setTextColor(loggerColor(), kBlack);
  statusSprite_.drawString(loggerShortStatus(), 1130, 18);
  statusSprite_.pushSprite(0, kTabHeight);
}

void DisplayUi::statusItem(int16_t x, const char* label, bool ok) {
  statusSprite_.setTextColor(ok ? kGreen : kRed, kBlack);
  statusSprite_.drawString(label, x, 18);
}

const MeasurementField* DisplayUi::field(uint8_t group, uint8_t zone) const noexcept {
  return model_.field(group, zone);
}

const char* DisplayUi::semanticName(FieldSemantic semantic) noexcept {
  switch (semantic) {
    case FieldSemantic::Iat: return "Ansaugluft";
    case FieldSemantic::SupplyVoltage: return "Versorgung";
    case FieldSemantic::Coolant: return "Kuehlmittel";
    case FieldSemantic::MotorLoad: return "Motorlast";
    case FieldSemantic::Rpm: return "RPM";
    case FieldSemantic::LambdaVoltage: return "Lambda";
    case FieldSemantic::LambdaTimer: return "Lambda-Zeit";
    case FieldSemantic::ProbeStatusCounter: return "Sondenstatus";
    case FieldSemantic::ThrottlePotVoltage: return "Drosselspannung";
    case FieldSemantic::InjectionTime: return "Einspritzzeit";
    case FieldSemantic::ThrottleAngleG69: return "G69";
    case FieldSemantic::IdleValveDuty: return "Leerlaufventil";
    case FieldSemantic::SpeedSignal: return "Geschwindigkeit";
    case FieldSemantic::LoadState: return "Lastzustand";
    case FieldSemantic::AdjustmentConditions: return "Einstellbeding.";
    default: return "Unbekannt";
  }
}

const char* DisplayUi::unitName(FieldUnit unit) noexcept {
  switch (unit) {
    case FieldUnit::Rpm: return "rpm";
    case FieldUnit::Celsius: return "C";
    case FieldUnit::Volt: return "V";
    case FieldUnit::Percent: return "%";
    case FieldUnit::Millisecond: return "ms";
    default: return "raw";
  }
}

uint16_t DisplayUi::valueColor(const MeasurementField& value,
                           const MeasurementSnapshot& snapshot) noexcept {
  return displayFieldStyle(value, snapshot) == DisplayFieldStyle::Normal ? TFT_WHITE : kGrey;
}

const char* DisplayUi::evidenceName(EvidenceGrade evidence) noexcept {
  switch (evidence) {
    case EvidenceGrade::Official: return "OFF";
    case EvidenceGrade::Reference: return "REF";
    case EvidenceGrade::Experimental: return "EXP";
    case EvidenceGrade::Inferred: return "INF";
    default: return "UNK";
  }
}

void DisplayUi::valueText(const MeasurementField& value, const MeasurementSnapshot& snapshot,
                      char* out, std::size_t size) noexcept {
  const DisplayFieldStyle style = displayFieldStyle(value, snapshot);
  if (style == DisplayFieldStyle::Invalid) {
    snprintf(out, size, "---");
  } else if (style == DisplayFieldStyle::RawUnknown) {
    snprintf(out, size, "RAW %u", static_cast<unsigned>(value.raw));
  } else if (value.unit == FieldUnit::Rpm) {
    snprintf(out, size, "RAW %u -> %.0f rpm", static_cast<unsigned>(value.raw),
             static_cast<double>(value.decodedValue));
  } else {
    snprintf(out, size, "RAW %u -> %.1f %s", static_cast<unsigned>(value.raw),
             static_cast<double>(value.decodedValue), unitName(value.unit));
  }
}

void DisplayUi::compactValueText(const MeasurementField& value, const MeasurementSnapshot& snapshot,
                             char* out, std::size_t size) noexcept {
  const DisplayFieldStyle style = displayFieldStyle(value, snapshot);
  if (style == DisplayFieldStyle::Invalid) {
    snprintf(out, size, "---");
  } else if (style == DisplayFieldStyle::RawUnknown) {
    snprintf(out, size, "RAW %u", static_cast<unsigned>(value.raw));
  } else if (value.unit == FieldUnit::Rpm) {
    snprintf(out, size, "%.0f rpm", static_cast<double>(value.decodedValue));
  } else {
    snprintf(out, size, "%.1f %s", static_cast<double>(value.decodedValue), unitName(value.unit));
  }
}

void DisplayUi::drawTile(const char* label, const MeasurementField* value, uint16_t fallbackRaw,
              int16_t x, int16_t y, int16_t w, int16_t h, bool allowFallback,
              bool requiresRunningEngine) {
  const auto& snapshot = model_.snapshot();
  const CompactFieldAvailability availability =
      requiresRunningEngine && value != nullptr &&
              displayFieldStyle(*value, snapshot) != DisplayFieldStyle::Invalid
          ? model_.compactAvailability(value->semantic)
          : CompactFieldAvailability::Available;
  tileSprite_.fillScreen(kBlack);
  tileSprite_.fillRoundRect(0, 0, w, h, 12, kPanel);
  tileSprite_.drawRoundRect(0, 0, w, h, 12, value ? valueColor(*value, snapshot) : kGrey);
  tileSprite_.setTextColor(kGrey, kPanel);
  tileSprite_.setTextSize(2);
  tileSprite_.drawString(label, 14, 12);
  char text[32]{};
  if (availability == CompactFieldAvailability::EngineStopped)
    snprintf(text, sizeof(text), "MOTOR AUS");
  else if (availability == CompactFieldAvailability::EngineStateUnknown)
    snprintf(text, sizeof(text), "STATUS ---");
  else if (value) compactValueText(*value, snapshot, text, sizeof(text));
  else if (allowFallback && snapshot.validity == SignalValidity::Valid)
    snprintf(text, sizeof(text), "RAW %u", static_cast<unsigned>(fallbackRaw));
  else snprintf(text, sizeof(text), "---");
  tileSprite_.setTextColor(value && availability == CompactFieldAvailability::Available
                               ? valueColor(*value, snapshot)
                               : kGrey,
                           kPanel);
  tileSprite_.setTextSize(3);
  tileSprite_.drawString(text, 14, h / 2 - 5);
  if (value) {
    char evidence[40]{};
    if (availability == CompactFieldAvailability::EngineStopped) {
      snprintf(evidence, sizeof(evidence), "ECU RAW %u | Motor aus",
               static_cast<unsigned>(value->raw));
    } else if (availability == CompactFieldAvailability::EngineStateUnknown) {
      snprintf(evidence, sizeof(evidence), "ECU RAW %u | RPM unbekannt",
               static_cast<unsigned>(value->raw));
    } else if (displayFieldStyle(*value, snapshot) == DisplayFieldStyle::Invalid) {
      snprintf(evidence, sizeof(evidence), "keine aktuellen Daten");
    } else {
      snprintf(evidence, sizeof(evidence), "RAW %u  S:%s F:%s",
               static_cast<unsigned>(value->raw), evidenceName(value->semanticEvidence),
               evidenceName(value->formulaEvidence));
    }
    tileSprite_.setTextColor(kGrey, kPanel);
    tileSprite_.setTextSize(2);
    tileSprite_.drawString(evidence, 14, h - 38);
  }
  tileSprite_.pushSprite(x, y);
}

void DisplayUi::drawCompact() {
  const int16_t w = tileSprite_.width();
  const int16_t h = tileSprite_.height();
  const int16_t y1 = kContentTop + 8;
  const int16_t y2 = y1 + h + kGap;
  const auto x = [w](uint8_t col) { return static_cast<int16_t>(kMargin + col * (w + kGap)); };
  drawTile("RPM", model_.preferredRpmField(), 0, x(0), y1, w, h, false);
  drawTile("KUEHLMITTEL", field(1, 2), model_.snapshot().coolantRaw, x(1), y1, w, h);
  drawTile("ANSAUGLUFT", field(2, 4), model_.snapshot().iatRaw, x(2), y1, w, h);
  drawTile("VERSORGUNG", field(2, 3), model_.snapshot().batteryRaw, x(3), y1, w, h);
  drawTile("MOTORLAST", field(3, 2), 0, x(0), y2, w, h, true, true);
  drawTile("G69 / DROSSEL", field(3, 3), model_.snapshot().g69Raw, x(1), y2, w, h);
  drawTile("EINSPRITZZEIT", field(2, 2), 0, x(2), y2, w, h, true, true);
  drawTile("LAMBDA-SPANNUNG", field(1, 3), 0, x(3), y2, w, h);
}

void DisplayUi::drawList() {
  const auto& snapshot = model_.snapshot();
  const uint8_t first = static_cast<uint8_t>(model_.listPage() * 13U);
  for (uint8_t row = 0; row < 13; ++row) {
    const uint8_t index = static_cast<uint8_t>(first + row);
    if (index >= snapshot.fieldCount) break;
    const auto& value = snapshot.fields[index];
    const uint16_t bg = row % 2 ? kPanel : kBlack;
    rowSprite_.fillScreen(bg);
    rowSprite_.setTextColor(valueColor(value, snapshot), bg);
    rowSprite_.setTextSize(2);
    char decoded[42]{};
    char text[150]{};
    valueText(value, snapshot, decoded, sizeof(decoded));
    snprintf(text, sizeof(text), "%03u/Z%u %-18s %-27s S:%s F:%s", value.group, value.zone,
             semanticName(value.semantic), decoded, evidenceName(value.semanticEvidence),
             evidenceName(value.formulaEvidence));
    rowSprite_.drawString(text, 10, 10);
    rowSprite_.pushSprite(kMargin, kContentTop + 4 + row * kRowHeight);
  }
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(kGrey, kBlack);
  M5.Display.drawString(model_.listPage() == 0 ? "Links: Seite 1 | rechts tippen: Seite 2" :
                                                 "Links tippen: Seite 1 | Seite 2",
                        20, height_ - 24);
}

void DisplayUi::drawSystem() {
  const auto& s = model_.snapshot();
  char lines[8][86]{};
  snprintf(lines[0], sizeof(lines[0]), "Transportgeneration: %lu", static_cast<unsigned long>(s.transportGeneration));
  snprintf(lines[1], sizeof(lines[1]), "Sessionepoch: %lu", static_cast<unsigned long>(s.sessionEpoch));
  snprintf(lines[2], sizeof(lines[2]), "Frames / letzte RX-Sequenz: %lu / %lu",
           static_cast<unsigned long>(s.frameCount), static_cast<unsigned long>(s.lastRxSequence));
  snprintf(lines[3], sizeof(lines[3]), "Drops frame/rx/snapshot: %lu / %lu / %lu",
           static_cast<unsigned long>(s.frameDrops), static_cast<unsigned long>(s.rxIngressDrops),
           static_cast<unsigned long>(s.snapshotOverwrites));
  snprintf(lines[4], sizeof(lines[4]), "Fault/Reject/Action/Byte: %lu / %lu / %lu / %u",
           static_cast<unsigned long>(s.faultCount), static_cast<unsigned long>(s.parserRejects),
           static_cast<unsigned long>(s.actionFailures), static_cast<unsigned>(s.byteFault));
  snprintf(lines[5], sizeof(lines[5]), "Snapshotstatus: %s", validityName(s.validity));
  snprintf(lines[6], sizeof(lines[6]), "Scope: %u / %u feste Samples", static_cast<unsigned>(model_.scope().size()),
           static_cast<unsigned>(model_.scope().capacity()));
  snprintf(lines[7], sizeof(lines[7]), "Displaydaten ausschliesslich aus MeasurementSnapshot");
  for (uint8_t row = 0; row < 8; ++row) {
    const uint16_t bg = row % 2 ? kPanel : kBlack;
    rowSprite_.fillScreen(bg);
    rowSprite_.setTextColor(TFT_WHITE, bg);
    rowSprite_.setTextSize(2);
    rowSprite_.drawString(lines[row], 12, 10);
    rowSprite_.pushSprite(kMargin, kContentTop + 18 + row * 56);
  }
}

const char* DisplayUi::validityName(SignalValidity value) noexcept {
  switch (value) {
    case SignalValidity::Valid: return "VALID";
    case SignalValidity::Stale: return "STALE";
    case SignalValidity::Disconnected: return "DISCONNECTED";
    case SignalValidity::Invalid: return "INVALID";
    default: return "UNKNOWN";
  }
}

void DisplayUi::drawTraces() {
  const int16_t w = scopeSprite_.width();
  const int16_t h = scopeSprite_.height();
  scopeSprite_.fillScreen(kBlack);
  scopeSprite_.drawRect(0, 0, w, h, kGrey);
  for (uint8_t i = 1; i < 5; ++i) scopeSprite_.drawFastHLine(1, i * h / 5, w - 2, 0x4208);
  const auto& ring = model_.scope();
  if (ring.size() < 2) {
    scopeSprite_.setTextColor(TFT_WHITE, kBlack);
    scopeSprite_.setTextSize(2);
    scopeSprite_.drawString("Warte auf Snapshotdaten...", 20, 20);
  } else {
    drawTraceLine(ring, 0, kGreen);
    drawTraceLine(ring, 1, kBlue);
    drawTraceLine(ring, 2, kAmber);
  }
  scopeSprite_.pushSprite(kMargin, kContentTop + 18);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(kGreen, kBlack); M5.Display.drawString("RPM", 30, kContentTop + kScopeHeight + 30);
  M5.Display.setTextColor(kBlue, kBlack); M5.Display.drawString("Kuehlmittel raw", 130, kContentTop + kScopeHeight + 30);
  M5.Display.setTextColor(kAmber, kBlack); M5.Display.drawString("Versorgung raw", 360, kContentTop + kScopeHeight + 30);
  M5.Display.setTextColor(kGrey, kBlack);
  M5.Display.drawString(model_.scopePaused() ? "PAUSE - tippen zum Fortsetzen" : "LIVE - tippen fuer Pause",
                        30, kContentTop + kScopeHeight + 66);
  if (needLoggerControls_) drawLoggerControls();
}

void DisplayUi::sendLoggerCommand(logging::LoggerCommandKind kind) noexcept {
  if (loggerCommands_ == nullptr ||
      !loggerCommands_->trySend({kind, static_cast<uint64_t>(esp_timer_get_time())})) {
    loggerCommandRejected_ = true;
  } else {
    loggerCommandRejected_ = false;
  }
  loggerDirty_ = true;
  needLoggerControls_ = true;
}

const char* DisplayUi::loggerShortStatus() const noexcept {
  switch (loggerStatus_.state) {
    case logging::LoggerState::Recording: return "LOG REC";
    case logging::LoggerState::Ready: return "LOG BEREIT";
    case logging::LoggerState::StorageFull: return "LOG VOLL";
    case logging::LoggerState::WriteError: return "LOG FEHLER";
    case logging::LoggerState::NoStorage: return "LOG KEINE SD";
    default: return "LOG INIT";
  }
}

uint16_t DisplayUi::loggerColor() const noexcept {
  switch (loggerStatus_.state) {
    case logging::LoggerState::Recording: return kGreen;
    case logging::LoggerState::Ready: return kGrey;
    case logging::LoggerState::Initializing: return kAmber;
    default: return kRed;
  }
}

void DisplayUi::drawLoggerControls() {
  needLoggerControls_ = false;
  const bool recording = loggerStatus_.state == logging::LoggerState::Recording;
  const uint16_t startColor = recording ? kRed : kGreen;
  M5.Display.fillRoundRect(kStartButtonX, kLoggerButtonY, kStartButtonW, kLoggerButtonH, 14,
                          startColor);
  M5.Display.setTextColor(TFT_WHITE, startColor);
  M5.Display.setTextSize(3);
  M5.Display.drawCentreString(recording ? "SPROTZEN STOP" : "SPROTZEN START",
                              kStartButtonX + kStartButtonW / 2,
                              kLoggerButtonY + 24);
  const uint16_t markerColor = recording ? kBlue : kPanel2;
  M5.Display.fillRoundRect(kMarkerButtonX, kLoggerButtonY, kMarkerButtonW, kLoggerButtonH, 14,
                          markerColor);
  M5.Display.setTextColor(recording ? TFT_WHITE : kGrey, markerColor);
  M5.Display.drawCentreString("MARKER", kMarkerButtonX + kMarkerButtonW / 2,
                              kLoggerButtonY + 24);
  char info[96]{};
  if (loggerCommandRejected_) {
    snprintf(info, sizeof(info), "LOGGER COMMAND-QUEUE VOLL");
  } else if (loggerStatus_.state == logging::LoggerState::Recording) {
    snprintf(info, sizeof(info), "REC %lu | Marker %lu | Drops %lu",
             static_cast<unsigned long>(loggerStatus_.snapshotsWritten),
             static_cast<unsigned long>(loggerStatus_.eventsWritten > 0
                                            ? loggerStatus_.eventsWritten - 1U : 0U),
             static_cast<unsigned long>(loggerStatus_.queueDrops));
  } else {
    snprintf(info, sizeof(info), "%s | Datensaetze %lu | Drops %lu", loggerShortStatus(),
             static_cast<unsigned long>(loggerStatus_.snapshotsWritten),
             static_cast<unsigned long>(loggerStatus_.queueDrops));
  }
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(loggerColor(), kBlack);
  M5.Display.drawString(info, kStartButtonX, kLoggerButtonY - 28);
}

void DisplayUi::drawTraceLine(const DisplayScopeRing<DisplayUiModel::kScopeCapacity>& ring,
                   uint8_t signal, uint16_t color) {
  const std::size_t count = ring.size();
  const int16_t w = scopeSprite_.width();
  const int16_t h = scopeSprite_.height();
  int16_t px = 0;
  int16_t py = traceY(ring.chronological(0), signal, h);
  for (std::size_t i = 1; i < count; ++i) {
    const int16_t x = static_cast<int16_t>(static_cast<int32_t>(w - 1) * i / (count - 1));
    const int16_t y = traceY(ring.chronological(i), signal, h);
    scopeSprite_.drawLine(px, py, x, y, color);
    px = x;
    py = y;
  }
}

int16_t DisplayUi::traceY(const DisplayScopeSample& sample, uint8_t signal, int16_t height) noexcept {
  uint16_t value = signal == 0 ? sample.rpm : signal == 1 ? sample.coolantRaw : sample.supplyRaw;
  const uint16_t maxValue = signal == 0 ? 5000 : 255;
  if (value > maxValue) value = maxValue;
  return static_cast<int16_t>((height - 1) - static_cast<int32_t>(height - 1) * value / maxValue);
}

}  // namespace digifant::ui
