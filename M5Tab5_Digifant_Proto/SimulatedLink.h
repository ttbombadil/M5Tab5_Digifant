#pragma once

#include "SerialLink.h"
#include "ReplayData.h"

// Simuliert ein KWP1281-Gegenueber, das auf eingehende write()-Aufrufe mit
// einer verzoegerten Antwort reagiert - statt (wie in der Vorversion)
// unabhaengig von tatsaechlichen Anfragen autonom auf eigener Zeitbasis
// Frames zu erzeugen. Dadurch verhaelt sich die Simulation wie ein
// realistisches Request/Response-Gegenueber, was insbesondere fuer die
// spaetere KWP1281-Protokolllogik (M3+) relevant ist.
//
// Die Antwortdaten stammen aus einem echten Motor-Capture und werden nach dem
// letzten vollstaendigen Block wieder ab dem ersten Block ausgegeben. Weitere
// Datensaetze koennen in ReplayData.h als Profile ergaenzt und hier ausgewaehlt
// werden, ohne den Transport zu aendern.
class SimulatedLink : public SerialLink {
public:
  bool begin(uint32_t baud) override;
  bool isConnected() override;
  int available() override;
  int read() override;
  size_t write(const uint8_t *buffer, size_t size) override;

  // Muss regelmaessig aus loop() aufgerufen werden, damit eine faellige
  // simulierte Antwort tatsaechlich im Lesepuffer bereitgestellt wird,
  // sobald die simulierte Antwortzeit abgelaufen ist.
  void update();

private:
  void generateReplayFrame();

  static constexpr size_t kBufferSize = 65;
  uint8_t _rxBuffer[kBufferSize] = {0};
  size_t _rxLen = 0;
  size_t _rxPos = 0;

  bool _responsePending = false;
  uint32_t _responseStartMs = 0;   // Zeitpunkt des letzten write()-Aufrufs
  uint32_t _responseDelayMs = 25;  // simulierte Antwortzeit des Steuergeraets (10 Hz Aktualisierung)

  size_t _replayIndex = 0;
};
