# Code-Review & Architektur-Analyse: KWP1281 / Digifant Logger

**Projekt:** KWP1281-Datenlogger für VW 2E (Digifant 1.7) auf M5Stack Tab5 (ESP32-P4)  
**Datum:** 19. August 2026  
**Fokus:** Embedded-Effizienz, Automotive-Robustheit, Echtzeitfähigkeit, Vermeidung von Heap-Fragmentierung

---

## 1. Zusammenfassung: Die 3 größten Schwachstellen

### 1.1 Synchron-blockierendes Bit-Banging & ACK-Handling (`EcuInitTester`)
- **Problem:** `send5BaudAddress()` blockiert die CPU mit harten `delay(200)`-Aufrufen für über **2200 ms**. Währenddessen stoppen Display-Aktualisierungen, Touch-Events und Task-Switches.
- Auch beim Senden regulärer Blöcke (`sendBlockWithHandshake`) wird für jedes einzelne Byte ein blockierendes Polling mit bis zu 350 ms Timeout durchgeführt.
- **Folge:** Gefahr von FreeRTOS Task-Watchdog-Resets (WDT), Verzögerungen im Dashboard und verpasste Zeitfenster beim Handshake.

### 1.2 Integer-Underflow bei Tabellen-Interpolation (`decodeNumberedGroup` / `decodeTemp8C`)
- **Problem:** Bei der linearen Interpolation (Formel `0x8B` und `0x8C`) wird `left + (right - left) * (mwb % 16) / 16.0f` gerechnet, wobei `left` und `right` als `uint8_t` deklariert sind. Da NTC-Temperaturkennlinien fallend sind ($left > right$), führt `right - left` im vorzeichenlosen 8-Bit-Bereich zu einem **Underflow** ($100 - 160 = 196$).
- **Folge:** Massive Temperatursprünge (z. B. rechnerisch > 3000 °C statt 85 °C).
- Zudem gibt es eine Diskrepanz zwischen Simulator (`SimulatedLink`) und Live-Parser (`EcuInitTester`) bezüglich der Payload-Indizes für Gruppe 000.

### 1.3 Heap-Fragmentierung im Hot-Path (`Console` & Dashboard-Logs)
- **Problem:** `Console::println` verwendet ein Array aus 40 dynamischen `String`-Objekten (`String _lines[40]`). Beim Scrollen werden 39 Strings im Speicher umkopiert und reallokiert.
- **Folge:** Permanente Heap-Fragmentierung auf dem ESP32 bei jedem empfangenen KWP1281-Block.

---

## 2. Detaillierte Code-Vergleiche ("Vorher" vs. "Optimiert")

### 2.1 Kennfeld-Interpolation (Formel 0x8C / NTC-Temperatur)

#### Vorher (`EcuInitTester.cpp` / `Dashboard.cpp`)
```cpp
// FEHLER: right - left ist bei fallender NTC-Kennlinie negativ -> uint8_t Underflow!
uint8_t left = header[headerPos + index];
uint8_t right = header[headerPos + index + 1];
float interpolated = left + (right - left) * (mwb % 16) / 16.0f;
value = (formula == 0x8B) ? (interpolated * nwb) : (interpolated - nwb);
```

#### Optimiert
```cpp
// Vorzeichenbehafteter Cast vor der Subtraktion verhindert den Underflow:
const int16_t left  = static_cast<int16_t>(header[headerPos + index]);
const int16_t right = static_cast<int16_t>(header[headerPos + index + 1]);
const uint8_t frac  = mwb & 0x0F; // mwb % 16 per Bitmaske

const float interpolated = static_cast<float>(left) + 
                           static_cast<float>(right - left) * (frac / 16.0f);

const float value = (formula == 0x8B) 
                    ? (interpolated * static_cast<float>(nwb)) 
                    : (interpolated - static_cast<float>(nwb));
```

#### Warum die Optimierung besser ist
- **Mathematisch korrekt:** Verhindert falsche Temperaturwerte bei NTC-Kennlinien.
- **Performance:** `mwb & 0x0F` ersetzt die teurere Modulo-Division `% 16`.

---

### 2.2 KWP1281 Byte-Echo & Quittierungslogik

#### Vorher (`EcuInitTester.cpp`)
```cpp
for (size_t i = 0; i < totalBytes; ++i) {
  uint8_t b = txBuf[i];
  _link.write(&b, 1);

  // 1. Eigenes K-Line TX-Echo abwarten und verwerfen
  uint32_t echoStart = millis();
  while (millis() - echoStart < 150) {
    if (_link.available() > 0) {
      int ch = _link.read();
      if (ch == b) break;
    }
    delay(1);
  }

  // 2. Für alle Bytes AUSSER dem letzten (0x03) antwortet die ECU mit ~b
  if (i < totalBytes - 1) {
    uint8_t expectedAck = static_cast<uint8_t>(~b);
    uint32_t ackStart = millis();
    bool gotAck = false;
    while (millis() - ackStart < 350) {
      if (_link.available() > 0) {
        int ch = _link.read();
        if (ch < 0) continue;
        uint8_t rx = static_cast<uint8_t>(ch);
        if (rx == expectedAck) {
          gotAck = true;
          break;
        }
      }
      delay(1);
    }
  }
}
```

#### Optimiert (Timeout-gesichert mit Task-Yield)
```cpp
bool EcuInitTester::waitForByte(uint8_t expected, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (_link.available() > 0) {
      int ch = _link.read();
      if (ch >= 0 && static_cast<uint8_t>(ch) == expected) {
        return true;
      }
    }
    yield(); // FreeRTOS Task-Switching & Watchdog Reset erlauben
  }
  return false;
}

bool EcuInitTester::sendBlockWithHandshake(uint8_t title, const uint8_t *payload, size_t payloadLen) {
  uint8_t blockLen = static_cast<uint8_t>(payloadLen + 3);
  uint8_t txBuf[36];
  txBuf[0] = blockLen;
  txBuf[1] = _blockCounter;
  txBuf[2] = title;
  if (payload && payloadLen > 0) {
    memcpy(&txBuf[3], payload, payloadLen);
  }
  txBuf[blockLen] = 0x03;

  const size_t totalBytes = blockLen + 1;

  for (size_t i = 0; i < totalBytes; ++i) {
    const uint8_t b = txBuf[i];
    _link.write(&b, 1);

    // 1. Eigenes TX-Echo abräumen (K-Line Transceiver spiegelt TX auf RX)
    if (!waitForByte(b, 60)) {
      console.printf("[KWP TX] Echo timeout für Byte 0x%02X\n", b);
    }

    // 2. Jedes Byte außer Endbyte (0x03) verlangt invertiertes ACK (~b) von der ECU
    if (i < totalBytes - 1) {
      const uint8_t expectedAck = static_cast<uint8_t>(~b);
      if (!waitForByte(expectedAck, 180)) {
        console.printf("[KWP TX] Fehlendes ACK 0x%02X für Byte 0x%02X\n", expectedAck, b);
        return false;
      }
    }
  }

  _blockCounter++;
  _lastTxTime = millis();
  return true;
}
```

#### Warum die Optimierung besser ist
- **Kein WDT-Reset:** `yield()` füttert den FreeRTOS-Watchdog.
- **Reale Buskennzeiten:** Timeouts von 350 ms auf realistische 180 ms verkürzt.
- **Fail-Fast:** Bricht bei fehlendem ACK sofort ab, statt fehlerhafte Restbytes auf den Bus zu schreiben.

---

### 2.3 Heap-Optimierung: Statischer Ringpuffer für die Konsole

#### Vorher (`Console.h` / `Console.cpp`)
```cpp
// Dynamische String-Objekte im Heap
String _lines[kBufferLines];

void Console::println(const String &line) {
  if (_lineCount < kBufferLines) {
    _lines[_lineCount++] = line;
  } else {
    // 39 Reallokationen & Kopieroperationen bei jeder Zeile!
    for (uint8_t i = 1; i < kBufferLines; ++i) {
      _lines[i - 1] = _lines[i];
    }
    _lines[kBufferLines - 1] = line;
  }
}
```

#### Optimiert (Statischer Circular Buffer)
```cpp
// Console.h:
static constexpr uint8_t kBufferLines = 40;
static constexpr size_t  kMaxLineLen  = 96;

char    _lines[kBufferLines][kMaxLineLen];
uint8_t _head  = 0;
uint8_t _count = 0;

// Console.cpp:
void Console::println(const char *line) {
  Serial.println(line);
  if (!_ready) return;

  strncpy(_lines[_head], line, kMaxLineLen - 1);
  _lines[_head][kMaxLineLen - 1] = '\0';

  _head = (_head + 1) % kBufferLines;
  if (_count < kBufferLines) {
    _count++;
  }
  _dirty = true;
}

const char* Console::getLine(uint8_t index) const {
  if (index >= _count) return "";
  uint8_t start = (_count == kBufferLines) ? _head : 0;
  uint8_t actualIdx = (start + index) % kBufferLines;
  return _lines[actualIdx];
}
```

#### Warum die Optimierung besser ist
- **0 Heap-Allokationen:** Eliminiert dynamische Speicherallokation vollständig.
- **$O(1)$ Schreibaufwand:** Neuer Eintrag überschreibt den ältesten Eintrag per Zeigerindex ohne Kopierschleife.

---

## 3. Automotive- & Digifant-Logik Checkliste

| Parameter | VAG / Digifant 1.7 Spezifikation | Aktueller Code Status | Empfehlung |
|---|---|---|---|
| **Batteriespannung (U)** | Formel `0x85`, NWb = `0x18` ($U = \text{MWB} \cdot \frac{24}{256}$) | Korrekt implementiert | Beibehalten |
| **Drehzahl (Gruppe 000)** | Schätzung: $(\text{raw} - 32) \times 35$ | Funktional, Schätzung | Gruppe 001 (Formel `0x8B`) als primäre Drehzahlquelle nutzen, sobald aktiv |
| **Drosselklappe G69** | Gruppe 003, Zone 3 (ADC-Rohwert $0..255$) | Skaliert auf Grad (`raw * 80/85`) | **Rohwert** belassen; keine Grad-Skalierung vorgaukeln |
| **Motor-Lauf-Status** | Stillstand: `0x80`, Lauf: `0x00` | Direkter Gleichheitscheck (`== 0x00`) | Auf Bitmaske `(runFlagRaw & 0x80) == 0` umstellen |
| **K-Line Busruhe** | $\ge 2600\,\text{ms}$ vor 5-Baud-Init | Eingehalten (`kLineIdleDurationMs = 2600`) | Beibehalten |
| **5-Baud-Bitdauer** | $200\,\text{ms} \pm 5\,\text{ms}$ pro Bit | Eingehalten | Langfristig per Hardware-Timer/RMT implementieren |
