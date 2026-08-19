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

  _lineCount = 0;
  _ready = true;
}

void Console::println(const String &line) {
  Serial.println(line);

  if (!_ready) {
    // Vor begin() nur seriell ausgeben, kein Display-Zugriff.
    return;
  }

  if (_lineCount < kBufferLines) {
    _lines[_lineCount++] = line;
  } else {
    for (uint8_t i = 1; i < kBufferLines; ++i) {
      _lines[i - 1] = _lines[i];
    }
    _lines[kBufferLines - 1] = line;
  }

  // Kein Redraw hier! Das Display-Update wird auf update() verschoben,
  // damit Logging selbst niemals das SPI-Fullscreen-Redraw ausloest
  // (siehe Console.h fuer die Begruendung).
  _dirty = true;
}

void Console::println() {
  println(String());
}

void Console::printf(const char *fmt, ...) {
  char buffer[200];

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

  println(String(buffer));
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

  uint8_t start = (_lineCount > _visibleLines) ? (_lineCount - _visibleLines) : 0;
  for (uint8_t i = start; i < _lineCount; ++i) {
    M5.Display.println(_lines[i]);
  }

  M5.Display.endWrite();
}
