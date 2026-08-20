#pragma once

// Kapselt EspUsbHost + EspUsbHostCdcSerial hinter dem SerialLink-Interface.
// Durch das Umdefinieren von private zu public erhalten wir Zugriff auf
// submitVendorSerialControl fuer praezises Bit-Banging und Signalsteuerung.
#define private public
#include <EspUsbHost.h>
#undef private

#include "SerialLink.h"

// Kapselt EspUsbHost + EspUsbHostCdcSerial hinter dem SerialLink-Interface.
//
// WICHTIG (offener Punkt aus project.md): Die genauen Feldnamen des
// Geraete-Info-Structs (hier unten nur vid/pid verwendet) sowie die exakten
// Signaturen von EspUsbHost::begin()/ready()/onDeviceConnected() waren zum
// Zeitpunkt der Erstellung nicht vollstaendig gegen den aktuellen
// EspUsbHost.h-Header verifizierbar. Vor dem ersten Kompilieren bitte mit
// der tatsaechlich installierten Bibliotheksversion abgleichen.
class UsbCdcLink : public SerialLink {
public:
  UsbCdcLink();

  bool begin(uint32_t baud) override;
  bool isConnected() override;
  int available() override;
  int read() override;
  size_t write(const uint8_t *buffer, size_t size) override;

  // Zusaetzlich zum SerialLink-Interface: true, sobald der USB-Host-Stack
  // selbst initialisiert ist (unabhaengig davon, ob schon ein CDC-Geraet
  // erkannt wurde).
  bool hostReady();

  // KWP1281-spezifische Erweiterungen fuer die 5-Baud-Init. Diese Methoden
  // sind nur am echten USB-CDC-Link verfuegbar (SimulatedLink braucht sie
  // nicht). Sie reichen die Aufrufe an EspUsbHostCdcSerial weiter.
  bool setBaudRate(uint32_t baud);
  bool setDtr(bool enable);
  bool setRts(bool enable);
  bool setBreak(bool enable);
  bool setLatencyTimer(uint8_t latencyMs);

private:
  EspUsbHost _host;
  EspUsbHostCdcSerial _serial;
};
