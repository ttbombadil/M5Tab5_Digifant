#include "Console.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

Console console;

void Console::begin(uint8_t textSize) {
  M5.Display.setTextSize(textSize);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);

  int16_t lineHeight = M5.Display.fontHeight();
  if (lineHeight <= 0) {
    lineHeight = 8; // konservativer Fallback, falls fontHeight() nicht verfuegbar ist
  }
  lineHeight *= textSize;

  int32_t visible = M5.Display.height() / lineHeight;
  if (visible < 1) {
    visible = 1;
  }
  if (visible > kBufferLines) {
    visible = kBufferLines;
  }
  _visibleLines = static_cast<uint8_t>(visible);

  _head = 0;
  _count = 0;
  _ready = true;
}

void Console::println(const char *line) {
  if (line != nullptr) {
    Serial.println(line);
  } else {
    Serial.println();
  }

  if (!_ready) {
    // Vor begin() nur seriell ausgeben, kein Display-Zugriff.
    return;
  }

  if (line != nullptr) {
    strncpy(_lines[_head], line, kMaxLineLen - 1);
    _lines[_head][kMaxLineLen - 1] = '\0';
  } else {
    _lines[_head][0] = '\0';
  }

  _head = (_head + 1) % kBufferLines;
  if (_count < kBufferLines) {
    _count++;
  }

  // Kein Redraw hier! Das Display-Update wird auf update() verschoben,
  // damit Logging selbst niemals das SPI-Fullscreen-Redraw ausloest
  // (siehe Console.h fuer die Begruendung).
  _dirty = true;
}

void Console::println(const String &line) {
  println(line.c_str());
}

void Console::println() {
  println(static_cast<const char*>(nullptr));
}

const char* Console::getLine(uint8_t index) const {
  if (index >= _count) {
    return "";
  }
  uint8_t start = (_count == kBufferLines) ? _head : 0;
  uint8_t actualIdx = (start + index) % kBufferLines;
  return _lines[actualIdx];
}

void Console::printf(const char *fmt, ...) {
  char buffer[kMaxLineLen];

  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  // Abschliessende Zeilenumbrueche entfernen, da println() selbst je einen
  // Umbruch fuer Serial UND Display erzeugt.
  size_t len = strlen(buffer);
  while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) {
    buffer[--len] = '\0';
  }

  println(buffer);
}

void Console::update() {
  if (!_ready || !_dirty || !_displayEnabled) {
    return;
  }
  const uint32_t now = millis();
  if (now - _lastRedrawMs < kMinRedrawIntervalMs) {
    return;
  }
  _lastRedrawMs = now;
  _dirty = false;
  redraw();
}

void Console::redraw() {
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);

  uint8_t start = (_count > _visibleLines) ? (_count - _visibleLines) : 0;
  for (uint8_t i = start; i < _count; ++i) {
    M5.Display.println(getLine(i));
  }

  M5.Display.endWrite();
}
