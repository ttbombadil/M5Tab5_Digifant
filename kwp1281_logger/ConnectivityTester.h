#pragma once

#include "SerialLink.h"

// M2-Meilenstein: periodischer Roh-Sende/Empfangstest gegen ein beliebiges
// SerialLink (Simulation oder echte USB-Hardware). Prueft NICHT das
// eigentliche KWP1281-Protokoll (keine 5-Baud-Init) - das ist bewusst erst
// Gegenstand von M3.
class ConnectivityTester {
public:
  explicit ConnectivityTester(SerialLink &serialLink, uint32_t intervalMs = 100,
                               uint32_t timeoutMs = 80);

  // Muss regelmaessig aus loop() aufgerufen werden.
  void update();

private:
  SerialLink &_link;
  uint32_t _intervalMs;
  uint32_t _timeoutMs;

  uint32_t _lastTx = 0;
  bool _waitingForResponse = false;
};
