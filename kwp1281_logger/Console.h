#pragma once

#include <Arduino.h>
#include <M5Unified.h>

// Schreibt jede Log-Zeile gleichzeitig auf die serielle Schnittstelle UND als
// scrollende "Konsole" auf das M5-Display. Haelt dazu einen kleinen
// Ringpuffer der letzten Zeilen und zeichnet bei jeder neuen Zeile die
// zuletzt sichtbaren Zeilen neu.
//
// Verwendung ueberall im Projekt statt direkter Serial.println()/printf():
//   console.println("Text");
//   console.printf("Wert=%d", x);
//
// Es gibt genau eine globale Instanz (siehe unten "extern Console console"),
// analog zum globalen "Serial"-Objekt selbst - eine Interface-Abstraktion
// lohnt sich hier (anders als bei SerialLink) nicht, da es aktuell nur eine
// sinnvolle Implementierung gibt.
class Console {
public:
  // textSize: Skalierungsfaktor der Schrift (wie M5.Display.setTextSize()).
  // Muss aufgerufen werden, NACHDEM M5.begin() gelaufen ist.
  void begin(uint8_t textSize = 3);

  void println(const String &line);
  // Leere Zeile ausgeben (analog zu Serial.println() ohne Argumente).
  void println();
  void printf(const char *fmt, ...);

  // Muss regelmaessig aus loop() aufgerufen werden. Fuehrt das eigentliche
  // (teure) Display-Redraw NUR aus, wenn seit dem letzten Redraw genuegend
  // Zeit vergangen ist UND es neue Zeilen gibt. So blockiert das Loggen
  // selbst (println/printf) nie mehr das teure SPI-Fullscreen-Redraw -
  // entscheidend fuer zeitkritische Pfade wie die KWP1281 ~KB2-Antwort,
  // die innerhalb von ca. 35 ms nach Empfang des Keybytes gesendet werden
  // muss und sonst durch ein Redraw mitten im Empfangs-Loop verzoegert wuerde.
  void update();
  void setDisplayEnabled(bool enabled) { _displayEnabled = enabled; }

  uint8_t getLineCount() const { return _lineCount; }
  const String& getLine(uint8_t index) const { return _lines[index]; }

private:
  void redraw();

  static constexpr uint8_t kBufferLines = 40;
  static constexpr uint32_t kMinRedrawIntervalMs = 100;

  String _lines[kBufferLines];
  uint8_t _lineCount = 0;
  uint8_t _visibleLines = 12;
  bool _ready = false;
  bool _dirty = false;
  bool _displayEnabled = true;
  uint32_t _lastRedrawMs = 0;
};

extern Console console;
