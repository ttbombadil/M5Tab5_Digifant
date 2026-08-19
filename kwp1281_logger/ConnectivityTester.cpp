#include "ConnectivityTester.h"

#include "Console.h"

namespace {
// Test-Frame wie in M2 spezifiziert: Sync-Byte 0x55, Laenge 0x01, Kommando 0x00.
const uint8_t kTestFrame[3] = {0x55, 0x01, 0x00};
// Sicherheitsbegrenzung, damit eine fehlerhafte Quelle mit sehr vielen
// gepufferten Bytes die loop() nicht blockiert.
constexpr int kMaxReadsPerUpdate = 64;
}  // namespace

ConnectivityTester::ConnectivityTester(SerialLink &serialLink, uint32_t intervalMs,
                                        uint32_t timeoutMs)
    : _link(serialLink), _intervalMs(intervalMs), _timeoutMs(timeoutMs) {}

void ConnectivityTester::update() {
  if (!_link.isConnected()) {
    return;
  }

  // millis() nur einmal pro update()-Aufruf ermitteln - konsistent fuer
  // Sende- und Timeout-Entscheidung und robuster gegen millis()-Ueberlauf.
  const uint32_t now = millis();

  // Phase 1: ggf. ein neues Test-Frame senden (nur wenn nicht bereits auf
  // eine Antwort gewartet wird).
  if (!_waitingForResponse && (now - _lastTx >= _intervalMs)) {
    _lastTx = now;
    _waitingForResponse = true;
    const size_t written = _link.write(kTestFrame, sizeof(kTestFrame));
    console.printf("[LINK] tx: 55 01 00 (%u bytes written)",
                    static_cast<unsigned>(written));
  }

  // Phase 2: alle verfuegbaren Antwortbytes abholen, aber begrenzt, damit
  // eine fehlerhafte Quelle die loop() nicht blockiert.
  int reads = 0;
  while (_link.available() > 0 && reads < kMaxReadsPerUpdate) {
    const int ch = _link.read();
    if (ch < 0) {
      break;
    }
    console.printf("[LINK] rx=%02X", static_cast<uint8_t>(ch));
    _waitingForResponse = false;
    ++reads;
  }

  // Phase 3: Timeout-Pruefung, falls noch keine Antwort kam.
  if (_waitingForResponse && (now - _lastTx >= _timeoutMs)) {
    console.println("[LINK] no response within timeout");
    _waitingForResponse = false;
  }
}
