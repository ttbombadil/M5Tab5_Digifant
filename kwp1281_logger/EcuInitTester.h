#pragma once

#include "UsbCdcLink.h"

// M3-Test: KWP1281-5-Baud-Initialisierung gegen das echte Steuergerät
// (VW 037 906 024 / Digifant 1.7) über den AutoDia K409 (FTDI FT232R).
//
// Ablauf pro update()-Aufruf (Zustandsmaschine):
//   IDLE        -> wartet, bis USB-CDC verbunden ist
//   SEND_5BAUD  -> Adressbyte 0x01 bei 5 Baud ausgeben
//                  (primär via setBaudRate(5)+write; falls der FTDI 5 Baud
//                  ablehnt, Hinweis loggen - Bit-Banging über DTR/RTS ist
//                  ein separater, hier noch nicht implementierter Fallback)
//   SWITCH_9600-> sofort auf 9600 Baud umschalten
//   WAIT_SYNC   -> auf Sync-Byte 0x55 + Schlüsselbytes warten (mit Timeout)
//   DONE/ERROR  -> Ergebnis loggen, dann zurück auf IDLE für nächsten Versuch
//
// Der Test ist bewusst robust gegen millis()-Ueberlauf und gegen einen
// fehlschlagenden 5-Baud-Wechsel: er protokolliert jedes Zwischenergebnis,
// damit am Fahrzeug klar wird, an welcher Stelle die Init scheitert.
class EcuInitTester {
public:
  explicit EcuInitTester(UsbCdcLink &link, uint32_t responseTimeoutMs = 3000,
                          uint32_t retryIntervalMs = 6000);

  void update();

private:
  enum class State : uint8_t {
    IDLE,
    LINE_IDLE_WAIT,
    SEND_5BAUD_BITBANG,
    SWITCH_9600,
    WAIT_SYNC_KEYBYTES,
    SEND_INVERTED_KEYWORD,
    RECEIVE_BLOCK,
    DONE,
    ERROR_,
  };

  void enterState(State next);
  void logState() const;
  void send5BaudAddress(uint8_t address);
  bool sendBlockWithHandshake(uint8_t title, const uint8_t *payload, size_t payloadLen);
  void parseBlock(const uint8_t *data, size_t len);
  void requestMeasurementGroup();
  void decodeNumberedGroup(uint8_t group, const uint8_t *header, size_t headerLen,
                           const uint8_t *body, size_t bodyLen);

  UsbCdcLink &_link;
  uint32_t _responseTimeoutMs;
  uint32_t _retryIntervalMs;

  State _state = State::IDLE;
  uint32_t _stateStartMs = 0;     // Zeitpunkt des letzten Zustandswechsels
  uint32_t _lastAttemptMs = 0;   // Zeitpunkt des letzten Init-Versuchs
  bool _baud5Supported = true;    // false, sobald setBaudRate(5) fehlschlug

  uint8_t _kb1 = 0;
  uint8_t _kb2 = 0;
  uint8_t _rxKeyBytesCount = 0;
  uint8_t _blockCounter = 0;
  uint32_t _lastTxTime = 0;

  // Block RX Buffer & State
  uint8_t _rxBlockBuf[128] = {0};
  size_t _rxBlockPos = 0;
  uint8_t _expectedLen = 0;
  bool _sessionActive = false;
  bool _identFinished = false;
  bool _rxCounterInitialized = false;
  uint8_t _expectedRxCounter = 0;
  uint8_t _measurementGroup = 0;
  bool _awaitingGroupBody = false;
  bool _groupRequestNeedsAck = false;
  bool _awaitingGroupSwitchAck = false;
  // Nach jedem eigenen ACK-Byte (TX) erzeugt die K-Line ein lokales Echo,
  // das als naechstes RX-Byte zurueckkommt. Statt dieses Echo anhand seines
  // Wertes (~letztesByte) zu erkennen - was bei zufaellig komplementaeren
  // ECU-Datenbytes das REALE naechste Byte faelschlich verwerfen kann -
  // wird hier deterministisch genau das naechste RX-Byte nach einer ACK-
  // Sendung ignoriert, unabhaengig von seinem Wert.
  bool _pendingRxEcho = false;
  uint8_t _groupHeader[5][64] = {{0}};
  uint8_t _groupHeaderLen[5] = {0};

  uint8_t _baudIndex = 0; // Fixiert auf 1200 Baud (Verifiziert am Fahrzeug)
  static constexpr uint32_t kTargetBauds[] = {1200, 9600, 4800, 10400};
  static constexpr size_t kNumBauds = sizeof(kTargetBauds) / sizeof(kTargetBauds[0]);
};
