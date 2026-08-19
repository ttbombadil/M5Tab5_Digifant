#include "SimulatedLink.h"

#include "Console.h"
#include "Dashboard.h"

bool SimulatedLink::begin(uint32_t /*baud*/) {
  console.println("[SIM] running in simulation mode");
  console.printf("[SIM] replay: engine_running_corrected (%u blocks, cyclic)\n",
                 static_cast<unsigned>(kEngineRunningReplayCount));
  _responsePending = false;
  _replayIndex = 0;
  dashboard.setMode("SIMULATION (Replay)");
  dashboard.setStage(ConnStage::BUS_INIT, "Verbindungsaufbau: 5-Baud-Init (1200 Baud)");
  return true;
}

bool SimulatedLink::isConnected() { return true; }

int SimulatedLink::available() {
  return static_cast<int>(_rxLen - _rxPos);
}

int SimulatedLink::read() {
  if (_rxPos >= _rxLen) {
    return -1;
  }
  return _rxBuffer[_rxPos++];
}

size_t SimulatedLink::write(const uint8_t *buffer, size_t size) {
  String line = "[SIM] tx: ";
  for (size_t i = 0; i < size; ++i) {
    char byteStr[4];
    snprintf(byteStr, sizeof(byteStr), "%02X ", buffer[i]);
    line += byteStr;
  }
  console.println(line);

  // Eine evtl. noch nicht abgeholte alte Antwort verwerfen - eine neue
  // Anfrage ersetzt die vorherige, wie es auch bei echter Hardware der Fall
  // waere (neuer Request start).
  _rxLen = 0;
  _rxPos = 0;

  _responsePending = true;
  _responseStartMs = millis();

  return size;
}

void SimulatedLink::update() {
  if (!_responsePending) {
    return;
  }
  // millis()-Differenz statt direktem Vergleich mit einem gespeicherten
  // Zeitpunkt - so funktioniert die Wartezeit auch korrekt ueber einen
  // millis()-Ueberlauf (ca. alle 49 Tage) hinweg.
  if (millis() - _responseStartMs < _responseDelayMs) {
    return;
  }

  _responsePending = false;
  generateReplayFrame();
}

void SimulatedLink::generateReplayFrame() {
  _rxLen = 0;
  _rxPos = 0;

  const size_t currentIdx = _replayIndex;
  const ReplayFrame &frame = kEngineRunningReplay[_replayIndex];
  const size_t copyLen = frame.length < kBufferSize ? frame.length : kBufferSize;
  for (size_t i = 0; i < copyLen; ++i) {
    _rxBuffer[_rxLen++] = frame.data[i];
  }
  _replayIndex = (_replayIndex + 1) % kEngineRunningReplayCount;

  console.printf("[SIM] replay block=%u group=%u title=0x%02X len=%u",
                 static_cast<unsigned>(currentIdx),
                 static_cast<unsigned>(frame.group), frame.data[2],
                 static_cast<unsigned>(_rxLen));

  dashboard.incBlockCount();

  const uint8_t title = frame.data[2];

  if (title == 0xF6) {
    // ECU-Identifikationsblock (Blocks 0..2)
    char identBuf[32] = {0};
    size_t payloadLen = frame.data[0] >= 3 ? frame.data[0] - 3 : 0;
    if (payloadLen > sizeof(identBuf) - 1) payloadLen = sizeof(identBuf) - 1;
    memcpy(identBuf, &frame.data[3], payloadLen);
    identBuf[payloadLen] = '\0';
    if (currentIdx == 0) {
      dashboard.setEcuInfo(String(identBuf));
      dashboard.setStage(ConnStage::IDENTIFIKATION, "Verbindungsaufbau: ECU Identifikation (Teil 1/3)");
    } else if (currentIdx == 1) {
      dashboard.setEcuInfo(String("037906024AG ") + String(identBuf));
      dashboard.setStage(ConnStage::IDENTIFIKATION, "Verbindungsaufbau: ECU Identifikation (Teil 2/3)");
    } else {
      dashboard.setStage(ConnStage::IDENTIFIKATION, "Verbindungsaufbau: ECU Identifikation abgeschlossen");
    }
  } else if (title == 0x09) {
    if (currentIdx <= 5) {
      dashboard.setStage(ConnStage::HANDSHAKE, "Handshake bestaetigt, starte Messwertgruppen");
    }
  } else if (title == 0xF4) {
    // Wenn es ein 14-Byte Frame ist (data[0] == 0x0D), ist es Gruppe 000!
    if (frame.data[0] == 0x0D && frame.length >= 14) {
      const uint8_t iatRaw = frame.data[3];
      const uint8_t coPotRaw = frame.data[4];
      const uint8_t coolantRaw = frame.data[5];
      const uint8_t rpmRaw = frame.data[6];
      const uint8_t lambdaRaw = frame.data[7];
      const uint8_t status = frame.data[8];
      const uint8_t runFlag = frame.data[9];
      const uint8_t injRaw = frame.data[11];
      const uint16_t rpm = rpmRaw > 32 ? static_cast<uint16_t>(rpmRaw - 32) * 35 : 0;
      dashboard.setGroup000(rpm, coolantRaw, iatRaw, status, runFlag, lambdaRaw, injRaw);
      dashboard.setStage(ConnStage::VERBUNDEN, "Verbunden (Messwertgruppe 000 empfangen)");
      console.printf("[SIM M5] group000 rpm=%u lambda=%u inj=%u status_raw=0x%02X engine=%s\n",
                     rpm, lambdaRaw, injRaw, status, (runFlag & 0x80) == 0 ? "RUNNING" : "STOPPED");
    } else if (frame.group == 1 && frame.length >= 8) {
      // Gruppe 001 Body
      dashboard.setStage(ConnStage::VERBUNDEN, "Verbunden (Gruppe 001: Kuehlmittel / IAT)");
    } else if (frame.group == 2 && frame.length >= 8) {
      // Gruppe 002 Body: Zone 3 (data[5]) ist Batteriespannung
      const uint8_t batteryRaw = frame.data[5];
      const float battery = static_cast<float>(batteryRaw) * 24.0f / 256.0f;
      dashboard.setBattery(battery);
      dashboard.setStage(ConnStage::VERBUNDEN, "Verbunden (Gruppe 002: Batterie)");
      console.printf("[SIM M5] Batterie raw=%u approx=%.2f V\n", batteryRaw, battery);
    } else if (frame.group == 3 && frame.length >= 8) {
      // Gruppe 003 Body: Zone 3 (data[5]) ist G69 Drosselklappenstellung
      const uint8_t g69 = frame.data[5];
      dashboard.setG69(g69);
      dashboard.setStage(ConnStage::VERBUNDEN, "Verbunden (Gruppe 003: G69 Klappe)");
      console.printf("[SIM M5] G69 raw=%u (relativ)\n", g69);
    } else if (frame.group == 4 && frame.length >= 8) {
      dashboard.setStage(ConnStage::VERBUNDEN, "Verbunden (Gruppe 004: Zyklen aktiv)");
    }
  } else if (title == 0x02) {
    dashboard.setStage(ConnStage::VERBUNDEN, String("Verbunden (Gruppe 00") + String(frame.group) + " Header)");
  }
}
