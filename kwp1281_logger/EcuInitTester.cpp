#include "EcuInitTester.h"

#include "Console.h"
#include "Dashboard.h"

namespace {
// KWP1281-Konstanten fuer die 5-Baud-Init des Digifant-Steuergeraets.
constexpr uint8_t  kAddrByte          = 0x01;   // Adressbyte fuer "Motorsteuergeraet"
constexpr uint32_t kBaud5              = 5;      // 5-Baud-Init-Baudrate
constexpr uint32_t kBaud9600           = 9600;   // Datenphase nach der Init
constexpr uint8_t  kSyncByte          = 0x55;    // erwartetes Sync-Byte der ECU-Antwort
constexpr uint32_t kLineIdleDurationMs = 2600;   // K-Line Idle vor 5-Baud-Puls (aus digidash)
constexpr uint32_t kBitTime5BaudMs    = 2000;   // 10 Bits @ 5 Baud = 2000 ms
constexpr uint32_t kKeyByteAckDelayMs = 40;     // Pause vor invertiertem Keybyte ~KB2
}  // namespace

constexpr uint32_t EcuInitTester::kTargetBauds[];

EcuInitTester::EcuInitTester(UsbCdcLink &link, uint32_t responseTimeoutMs,
                              uint32_t retryIntervalMs)
    : _link(link),
      _responseTimeoutMs(responseTimeoutMs),
      _retryIntervalMs(retryIntervalMs) {}

void EcuInitTester::enterState(State next) {
  _state = next;
  _stateStartMs = millis();

  // Grob verstaendliche Statuszusammenfassung fuer das Dashboard, getrennt
  // von den detaillierten technischen Zustandsnamen in logState()/Console.
  switch (_state) {
    case State::IDLE:                   dashboard.setStage(ConnStage::BEREIT, "Warte auf USB-Geraet"); break;
    case State::LINE_IDLE_WAIT:         dashboard.setStage(ConnStage::BUS_INIT, "Verbindungsaufbau: K-Line Ruhephase (2.6s)"); break;
    case State::SEND_5BAUD_BITBANG:     dashboard.setStage(ConnStage::BUS_INIT, "Verbindungsaufbau: 5-Baud-Init Adressbyte 0x01"); break;
    case State::SWITCH_9600:            dashboard.setStage(ConnStage::HANDSHAKE, "Verbindungsaufbau: Baudratenwechsel"); break;
    case State::WAIT_SYNC_KEYBYTES:     dashboard.setStage(ConnStage::HANDSHAKE, "Verbindungsaufbau: warte auf Sync/Keybytes"); break;
    case State::SEND_INVERTED_KEYWORD:  dashboard.setStage(ConnStage::HANDSHAKE, "Verbindungsaufbau: Keybyte-Handshake (~KB2)"); break;
    case State::RECEIVE_BLOCK:          dashboard.setStage(_identFinished ? ConnStage::VERBUNDEN : ConnStage::IDENTIFIKATION,
                                                           _identFinished ? "Verbunden (Messwerte)" : "Verbunden (ECU-Identifikation)"); break;
    case State::DONE:                   dashboard.setStage(ConnStage::VERBUNDEN, "Verbunden"); break;
    case State::ERROR_:                 dashboard.setStage(ConnStage::FEHLER, "Fehler / Neustart der Verbindung"); break;
  }
}

void EcuInitTester::logState() const {
  const char *name = "?";
  switch (_state) {
    case State::IDLE:                   name = "IDLE"; break;
    case State::LINE_IDLE_WAIT:         name = "LINE_IDLE_WAIT (2.6s)"; break;
    case State::SEND_5BAUD_BITBANG:     name = "SEND_5BAUD_BITBANG"; break;
    case State::SWITCH_9600:            name = "SWITCH_BAUD"; break;
    case State::WAIT_SYNC_KEYBYTES:     name = "WAIT_SYNC_KEYBYTES"; break;
    case State::SEND_INVERTED_KEYWORD:  name = "SEND_INVERTED_KEYWORD"; break;
    case State::RECEIVE_BLOCK:          name = "RECEIVE_BLOCK"; break;
    case State::DONE:                   name = "DONE"; break;
    case State::ERROR_:                 name = "ERROR"; break;
  }
  console.printf("[ECU] state=%s\n", name);
}

void EcuInitTester::send5BaudAddress(uint8_t address) {
  console.printf("[ECU] Bit-banging 5-baud address 0x%02X (200ms/bit)...", address);

  // 1. Startbit: 0 (LOW / Break ON)
  _link.setBreak(true);
  delay(200);

  // 2. 8 Datenbits (LSB first)
  for (uint8_t i = 0; i < 8; ++i) {
    bool bit = (address >> i) & 0x01;
    if (bit) {
      _link.setBreak(false); // 1 = HIGH (Marking)
    } else {
      _link.setBreak(true);  // 0 = LOW (Spacing / Break)
    }
    delay(200);
  }

  // 3. Stopbit: 1 (HIGH / Break OFF)
  _link.setBreak(false);
  delay(200);

  console.println("[ECU] 5-baud address transmission finished.");
}

bool EcuInitTester::waitForByte(uint8_t expected, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (_link.available() > 0) {
      int ch = _link.read();
      if (ch >= 0 && static_cast<uint8_t>(ch) == expected) {
        return true;
      }
    }
    yield();
  }
  return false;
}

bool EcuInitTester::sendBlockWithHandshake(uint8_t title, const uint8_t *payload, size_t payloadLen) {
  // Vollständiger KWP1281 Block: [Länge] [Counter] [Titel] [Payload...] [0x03]
  uint8_t blockLen = static_cast<uint8_t>(payloadLen + 3);
  uint8_t txBuf[36];
  txBuf[0] = blockLen;
  txBuf[1] = _blockCounter;
  txBuf[2] = title;
  if (payload && payloadLen > 0) {
    memcpy(&txBuf[3], payload, payloadLen);
  }
  txBuf[blockLen] = 0x03; // End of block

  size_t totalBytes = blockLen + 1;
  console.printf("[KWP TX] Block Title=0x%02X (ctr=%02X, len=%u)...\n", title, _blockCounter, static_cast<unsigned>(totalBytes));

  for (size_t i = 0; i < totalBytes; ++i) {
    const uint8_t b = txBuf[i];

    // Byte senden
    _link.write(&b, 1);

    // 1. Eigenes K-Line TX-Echo abwarten und verwerfen
    if (!waitForByte(b, 60)) {
      console.printf("[KWP TX] Echo timeout for byte 0x%02X\n", b);
    }

    // 2. Für alle Bytes AUSSER dem letzten (0x03) antwortet die ECU mit ~b
    if (i < totalBytes - 1) {
      const uint8_t expectedAck = static_cast<uint8_t>(~b);
      if (!waitForByte(expectedAck, 180)) {
        console.printf("[KWP TX] Missing ACK 0x%02X for byte 0x%02X\n", expectedAck, b);
        return false;
      }
    }
  }

  _blockCounter++;
  _lastTxTime = millis();
  return true;
}

void EcuInitTester::requestMeasurementGroup() {
  if (_measurementGroup == 0) {
    _awaitingGroupBody = false;
    sendBlockWithHandshake(0x12, nullptr, 0);
    return;
  }

  uint8_t group = _measurementGroup;
  if (_groupRequestNeedsAck) {
    // A group switch is preceded by a host ACK. The immediate header->body
    // follow-up deliberately skips this ACK (Digifant stream latch).
    sendBlockWithHandshake(0x09, nullptr, 0);
    _groupRequestNeedsAck = false;
    _awaitingGroupBody = false;
    _awaitingGroupSwitchAck = true;
    _lastTxTime = millis();
    return;
  }
  _awaitingGroupBody = true;
  sendBlockWithHandshake(0x29, &group, 1);
}

void EcuInitTester::decodeNumberedGroup(uint8_t group, const uint8_t *header,
                                        size_t headerLen, const uint8_t *body,
                                        size_t bodyLen) {
  console.printf("[M4 GROUP %u] header=%u body=%u: ", group,
                 static_cast<unsigned>(headerLen), static_cast<unsigned>(bodyLen));

  size_t headerPos = 0;
  size_t bodyPos = 0;
  uint8_t zone = 1;
  while (headerPos + 3 <= headerLen && bodyPos < bodyLen) {
    uint8_t formula = header[headerPos++];
    uint8_t nwb = header[headerPos++];
    uint8_t tableLen = header[headerPos++];
    if (headerPos + tableLen > headerLen) {
      console.printf("invalid-header-at-%u", static_cast<unsigned>(headerPos - 3));
      break;
    }

    uint8_t mwb = body[bodyPos++];
    bool decoded = false;
    float value = 0.0f;
    if ((formula == 0x8B || formula == 0x8C) && tableLen == 17) {
      uint8_t index = mwb >> 4;
      if (index > 15) index = 15;
      const int16_t left = static_cast<int16_t>(header[headerPos + index]);
      const int16_t right = static_cast<int16_t>(header[headerPos + index + 1]);
      const uint8_t frac = mwb & 0x0F;
      const float interpolated = static_cast<float>(left) + static_cast<float>(right - left) * (frac / 16.0f);
      value = (formula == 0x8B) ? (interpolated * static_cast<float>(nwb)) : (interpolated - static_cast<float>(nwb));
      decoded = true;
    } else if (formula == 0x85) {
      value = static_cast<float>(nwb) * mwb / 256.0f;
      decoded = true;
    } else if (formula == 0x88) {
      value = static_cast<float>(mwb & nwb);
      decoded = true;
    } else if (formula == 0x89) {
      value = static_cast<float>(nwb) * mwb * 0.01f;
      decoded = true;
    }

    if (decoded) {
      console.printf("z%u f=%02X raw=%u val=%.2f; ", zone, formula, mwb, value);
    } else {
      console.printf("z%u f=%02X raw=%u; ", zone, formula, mwb);
    }
    if (group == 2 && zone == 3 && formula == 0x85) {
      console.printf("Batterie ECU=%.2f V; ", value);
      dashboard.setBattery(value);
    } else if (group == 3 && zone == 3) {
      console.printf("G69 raw=%u (relative; no absolute degree scale); ", mwb);
      dashboard.setG69(mwb);
    }
    headerPos += tableLen;
    ++zone;
  }
  console.println();
}

void EcuInitTester::parseBlock(const uint8_t *data, size_t len) {
  if (len < 4 || data[0] + 1 != len || data[len - 1] != 0x03) {
    console.printf("[KWP RX] rejected invalid block len=%u\n", static_cast<unsigned>(len));
    return;
  }
  dashboard.incBlockCount();
  uint8_t title = data[2];
  // data[0]=len, data[1]=counter, data[2]=title, data[3..len-2]=payload, data[len-1]=0x03
  // Nutzdatenlänge = len - 4 (abzgl. Länge, Counter, Titel und Endbyte 0x03)
  size_t payloadLen = (len >= 4) ? (len - 4) : 0;
  const uint8_t *payload = &data[3];

  if (title == 0xF6) {
    char asciiStr[64] = {0};
    size_t copyLen = (payloadLen < sizeof(asciiStr) - 1) ? payloadLen : sizeof(asciiStr) - 1;
    memcpy(asciiStr, payload, copyLen);
    asciiStr[copyLen] = '\0';
    console.printf("\n>>> [ECU IDENT] '%s' <<<\n", asciiStr);
    dashboard.setEcuInfo(String(asciiStr));
  } else if (title == 0x09) {
    console.println("[KWP] RX ACK from ECU");
  } else if (title == 0xF4) {
    console.printf("[M4 GROUP %u BODY] (%u bytes): ", _measurementGroup,
                   static_cast<unsigned>(payloadLen));
    for (size_t i = 0; i < payloadLen; ++i) {
      console.printf("%02X ", payload[i]);
    }
    console.println();

    if (_measurementGroup == 0 && payloadLen == 10) {
      // Group 000 fields are raw except for the observed RPM estimate.
      uint8_t iatRaw = payload[0];
      uint8_t coPotRaw = payload[1];
      uint8_t coolantRaw = payload[2];
      uint8_t rpmRaw = payload[3];
      uint8_t lambdaRaw = payload[4];
      uint8_t statusRaw = payload[5];
      uint8_t runFlagRaw = payload[6];
      uint8_t group000Raw = payload[7];
      uint8_t injRaw = payload[8];
      uint8_t finalRaw = payload[9];

      int rpmEst = (rpmRaw > 32) ? (rpmRaw - 32) * 35 : 0;
      bool engineRunning = (runFlagRaw & 0x80) == 0;

      console.printf("  -> RPM: %d | Gruppe000 raw[7]=0x%02X raw[9]=0x%02X\n",
             rpmEst, group000Raw, finalRaw);
      console.printf("  -> Motor: %s (Flag 0x%02X) | KühlmittelRaw: %u | IAT: %u | Lambda: %u | CO-Pot: %u | InjRaw: %u | StatusRaw: 0x%02X\n",
             engineRunning ? "RUNNING" : "STOPPED", runFlagRaw, coolantRaw,
             iatRaw, lambdaRaw, coPotRaw, injRaw, statusRaw);
      dashboard.setGroup000(static_cast<uint16_t>(rpmEst), coolantRaw, iatRaw,
                            statusRaw, runFlagRaw, lambdaRaw, injRaw);
    }
  } else if (title == 0x02) {
    if (_measurementGroup >= 1 && _measurementGroup <= 4 && payloadLen <= 64) {
      memcpy(_groupHeader[_measurementGroup], payload, payloadLen);
      _groupHeaderLen[_measurementGroup] = static_cast<uint8_t>(payloadLen);
      console.printf("[M4 HEADER group %u] (%u bytes)\n", _measurementGroup,
                     static_cast<unsigned>(payloadLen));
    } else {
      console.printf("[M4 HEADER 0x02] (%u bytes)\n", static_cast<unsigned>(payloadLen));
    }
  } else if (title == 0x0A) {
    console.println("[KWP] Group Refused / Not Supported (0x0A)");
  } else {
    console.printf("[KWP] Block Title=0x%02X (%u bytes)\n", title, static_cast<unsigned>(payloadLen));
  }
}

void EcuInitTester::update() {
  const uint32_t now = millis();

  switch (_state) {
    case State::IDLE: {
      if (!_link.isConnected()) {
        return;  // auf USB-CDC-Geraet warten
      }
      // Nach Ablauf des Retry-Intervalls (oder beim ersten Mal) starten.
      if (now - _lastAttemptMs < _retryIntervalMs && _lastAttemptMs != 0) {
        return;
      }
      _lastAttemptMs = now;
      console.println("[ECU] starting KWP1281 init sequence");
      // K-Line Ruhepegel herstellen
      _link.setRts(false);
      _link.setDtr(false);
      enterState(State::LINE_IDLE_WAIT);
      logState();
      break;
    }

    case State::LINE_IDLE_WAIT: {
      // Mindestens 2600 ms Busruhe vor 5-Baud-Init (Quelle: digidash / KWP1281 Spezifikation)
      if (now - _stateStartMs < kLineIdleDurationMs) {
        return;
      }
      enterState(State::SEND_5BAUD_BITBANG);
      logState();
      break;
    }

    case State::SEND_5BAUD_BITBANG: {
      uint32_t activeBaud = kTargetBauds[_baudIndex];
      // Vorab auf Ziel-Baudrate konfigurieren, damit der Empfänger schon bereit ist
      _link.setBaudRate(activeBaud);
      _link.setLatencyTimer(1); // FTDI Latency Timer auf 1 ms für minimale Latenz

      // Puffer leeren
      while (_link.available() > 0) {
        _link.read();
      }

      // Echtes 5-Baud Bit-Banging (2000 ms Dauer für 10 Bits)
      send5BaudAddress(kAddrByte);

      // Puffer erneut leeren (etwaige Echos durch das Umschalten verwerfen)
      delay(50);
      while (_link.available() > 0) {
        _link.read();
      }

      console.printf("[ECU] waiting at %u baud for Sync(0x55) + Keybytes...\n", activeBaud);
      _rxKeyBytesCount = 0;
      _kb1 = 0;
      _kb2 = 0;
      enterState(State::WAIT_SYNC_KEYBYTES);
      break;
    }

    case State::SWITCH_9600: {
      // (Wird bei Bit-Banging nicht mehr separat benötigt, da vorab auf 9600 Baud geschaltet)
      enterState(State::WAIT_SYNC_KEYBYTES);
      break;
    }

    case State::WAIT_SYNC_KEYBYTES: {
      while (_link.available() > 0) {
        const int ch = _link.read();
        if (ch < 0) break;
        const uint8_t b = static_cast<uint8_t>(ch);

        if (_rxKeyBytesCount == 0) {
          if (b == kSyncByte) {
            console.printf("[ECU] rx Sync: 0x%02X (KWP1281 SYNC OK!)\n", b);
            _rxKeyBytesCount = 1;
          }
        } else if (_rxKeyBytesCount == 1) {
          _kb1 = b;
          console.printf("[ECU] rx Keybyte 1: 0x%02X\n", _kb1);
          _rxKeyBytesCount = 2;
        } else if (_rxKeyBytesCount == 2) {
          _kb2 = b;
          console.printf("[ECU] rx Keybyte 2: 0x%02X\n", _kb2);
          _rxKeyBytesCount = 3;

          // Puffer leeren, damit Sync & Keybytes nicht in RECEIVE_BLOCK verbleiben
          while (_link.available() > 0) {
            _link.read();
          }

          enterState(State::SEND_INVERTED_KEYWORD);
          return;
        }
      }

      if (now - _stateStartMs >= _responseTimeoutMs) {
        console.printf("[ECU] Timeout at %u baud waiting for Sync\n", kTargetBauds[_baudIndex]);
        _baudIndex = (_baudIndex + 1) % kNumBauds;
        enterState(State::ERROR_);
      }
      break;
    }

    case State::SEND_INVERTED_KEYWORD: {
      // Verzögerung vor ~KB2 (25..40 ms nach Keybyte 2)
      if (now - _stateStartMs < 35) {
        return;
      }
      const uint8_t invKb2 = ~_kb2;
      size_t written = _link.write(&invKb2, 1);
      console.printf("[ECU] TX ~KB2 ack: 0x%02X (written=%u)\n", invKb2, static_cast<unsigned>(written));
      _blockCounter = 0;
      _lastTxTime = millis();
      _rxBlockPos = 0;
      _expectedLen = 0;
      _pendingRxEcho = false;
      _sessionActive = true;
      _identFinished = false;
      _rxCounterInitialized = false;
      _measurementGroup = 0;
      _awaitingGroupBody = false;
      _groupRequestNeedsAck = false;
      _awaitingGroupSwitchAck = false;
      memset(_groupHeaderLen, 0, sizeof(_groupHeaderLen));

      console.println("[ECU] Handshake done! Listening for ECU blocks...");
      enterState(State::RECEIVE_BLOCK);
      break;
    }

    case State::RECEIVE_BLOCK: {
      // A group-switch ACK occupies its own host turn. Issue the following
      // 0x29 request only from the next loop iteration.
      if (_measurementGroup >= 1 && _measurementGroup <= 4 &&
          !_awaitingGroupBody && !_groupRequestNeedsAck &&
          !_awaitingGroupSwitchAck) {
        requestMeasurementGroup();
      }

      while (_link.available() > 0) {
        const int ch = _link.read();
        if (ch < 0) break;
        const uint8_t b = static_cast<uint8_t>(ch);

        if (_rxBlockPos == 0) {
          // Zweitstufen-Sync / Wiederholung abfangen
          if (b == 0x55 || b == _kb1) {
            continue;
          }
          if (b == _kb2) {
            console.printf("[KWP] 2nd Keybyte 2: 0x%02X -> Responding ~KB2 (0x%02X)!\n", b, static_cast<uint8_t>(~_kb2));
            delay(35);
            uint8_t inv = ~_kb2;
            _link.write(&inv, 1);
            continue;
          }

          // Gültige KWP1281 Blocklänge (3..64)
          // Das Längenbyte b gibt an, wie viele Bytes (Counter, Titel, Daten, Endbyte 0x03) folgen.
          if (b >= 3 && b <= 64 && b + 1 <= sizeof(_rxBlockBuf)) {
            _expectedLen = b;
            _rxBlockBuf[_rxBlockPos++] = b;

            // Länge mit ~len quittieren
            uint8_t inv = ~b;
            _link.write(&inv, 1);
            _pendingRxEcho = true;
            console.printf("[KWP RX] Block Length=0x%02X (%u bytes follow) -> TX ~len=0x%02X\n", b, b, inv);
          } else {
            console.printf("[KWP RX] stray byte 0x%02X\n", b);
          }
        } else {
          // Deterministisch genau das naechste RX-Byte nach einer eigenen
          // ACK-Sendung verwerfen (lokales K-Line-Echo) - unabhaengig von
          // seinem Wert. Ein wertbasierter Vergleich (b == ~letztesByte)
          // wuerde ein REALES ECU-Byte verwerfen, wenn es zufaellig
          // komplementaer zum vorherigen Byte ist (kommt bei langen
          // Headern/Bloecken vor und verschiebt dann das Endbyte 0x03).
          if (_pendingRxEcho) {
            _pendingRxEcho = false;
            continue;
          }

          _rxBlockBuf[_rxBlockPos++] = b;

          // Gesamte Byteanzahl des Blocks inklusive Längenbyte ist (_expectedLen + 1).
          // Alle Bytes von Index 0 bis Index (_expectedLen - 1) müssen quittiert werden.
          // Nur das allerletzte Byte (Index _expectedLen, Wert 0x03) wird NICHT quittiert!
          if (_rxBlockPos < static_cast<size_t>(_expectedLen + 1)) {
            // Dies ist ein Datenbyte (oder Counter/Titel) vor dem Endbyte 0x03 -> Quittieren!
            uint8_t inv = ~b;
            _link.write(&inv, 1);
            _pendingRxEcho = true;
            console.printf("[KWP TX ~b] 0x%02X (rx byte[%d]=0x%02X)\n", inv, _rxBlockPos - 1, b);
          } else {
            // Block vollständig empfangen; reject a wrong terminator before parsing.
            console.printf("[KWP RX END] 0x%02X (total %u bytes)\n", b, static_cast<unsigned>(_rxBlockPos));
            if (b != 0x03) {
              console.printf("[KWP RX] invalid end byte 0x%02X, resync\n", b);
              _rxBlockPos = 0;
              _expectedLen = 0;
              _pendingRxEcho = false;
              continue;
            }

            uint8_t rxCounter = _rxBlockBuf[1];
            if (!_rxCounterInitialized) {
              _rxCounterInitialized = true;
              // RX and TX blocks share one counter sequence. The host sends
              // an ACK block between two ECU blocks, so the next ECU counter
              // normally advances by two.
              _expectedRxCounter = static_cast<uint8_t>(rxCounter + 2);
            } else if (rxCounter != _expectedRxCounter) {
              console.printf("[KWP RX] counter error: got=%02X expected=%02X, resync\n",
                             rxCounter, _expectedRxCounter);
              _rxBlockPos = 0;
              _expectedLen = 0;
              _pendingRxEcho = false;
              enterState(State::ERROR_);
              return;
            } else {
              _expectedRxCounter = static_cast<uint8_t>(rxCounter + 2);
            }

            parseBlock(_rxBlockBuf, _rxBlockPos);
            uint8_t title = _rxBlockBuf[2];
            size_t blockPayloadLen = _rxBlockPos >= 4 ? _rxBlockPos - 4 : 0;
            _blockCounter = _rxBlockBuf[1] + 1; // Counter für unsere Antwort übernehmen
            _rxBlockPos = 0;
            _expectedLen = 0;
            _pendingRxEcho = false;

            delay(30);

            if (!_identFinished) {
              if (title == 0x09) {
                _identFinished = true;
                console.println("\n[M4] IDENT FINISHED; starting groups 000..004");
                _measurementGroup = 0;
                requestMeasurementGroup();
              } else {
                sendBlockWithHandshake(0x09, nullptr, 0); // ACK für nächsten ID-Block
              }
            } else {
              if (title == 0x09 && _awaitingGroupSwitchAck) {
                _awaitingGroupSwitchAck = false;
                requestMeasurementGroup();
              } else if (title == 0x02 && _measurementGroup >= 1 && _measurementGroup <= 4) {
                _awaitingGroupBody = true;
                requestMeasurementGroup();
              } else if (title == 0xF4 && _measurementGroup >= 1 && _measurementGroup <= 4 &&
                         _awaitingGroupBody) {
                decodeNumberedGroup(_measurementGroup,
                                    _groupHeader[_measurementGroup],
                                    _groupHeaderLen[_measurementGroup],
                                    &_rxBlockBuf[3], blockPayloadLen);
                _awaitingGroupBody = false;
                _measurementGroup = static_cast<uint8_t>((_measurementGroup + 1) % 5);
                _groupRequestNeedsAck = true;
                requestMeasurementGroup();
              } else if (title == 0x0A && _measurementGroup >= 1 && _measurementGroup <= 4) {
                console.printf("[M4 GROUP %u] refused, advancing\n", _measurementGroup);
                _awaitingGroupBody = false;
                _measurementGroup = static_cast<uint8_t>((_measurementGroup + 1) % 5);
                _groupRequestNeedsAck = true;
                requestMeasurementGroup();
              } else {
                _measurementGroup = 1;
                _groupRequestNeedsAck = true;
                requestMeasurementGroup();
              }
            }
            return;
          }
        }
      }

      if (now - _lastTxTime >= 4000) {
        console.println("[KWP] Session Timeout -> Restarting Init");
        enterState(State::ERROR_);
      }
      break;
    }

    case State::DONE:
    case State::ERROR_: {
      if (now - _stateStartMs >= _retryIntervalMs) {
        enterState(State::IDLE);
      }
      break;
    }
  }
}
