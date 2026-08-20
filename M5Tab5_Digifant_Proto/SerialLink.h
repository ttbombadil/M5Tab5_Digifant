#pragma once

#include <Arduino.h>

// Gemeinsame Abstraktion für "irgendeine byteorientierte serielle Verbindung".
// Sowohl die Simulation (SimulatedLink) als auch die echte USB-CDC-Hardware
// (UsbCdcLink) implementieren dieses Interface. Dadurch kann die spätere
// KWP1281-Protokolllogik (M3+) exakt gleich gegen beide Implementierungen
// laufen, ohne selbst zwischen Simulation und Hardware unterscheiden zu
// müssen.
class SerialLink {
public:
  virtual ~SerialLink() = default;

  // Initialisiert die Verbindung mit der angegebenen Baudrate.
  // Rueckgabe false, wenn die Initialisierung fehlgeschlagen ist.
  virtual bool begin(uint32_t baud) = 0;

  // true, wenn aktuell ein Gegenueber verbunden ist (bei der Simulation
  // immer true).
  virtual bool isConnected() = 0;

  // Anzahl der aktuell lesbaren Bytes.
  virtual int available() = 0;

  // Liest ein einzelnes Byte (-1, wenn keins verfuegbar ist).
  virtual int read() = 0;

  // Schreibt "size" Bytes aus "buffer". Rueckgabe: Anzahl geschriebener Bytes.
  virtual size_t write(const uint8_t *buffer, size_t size) = 0;
};
