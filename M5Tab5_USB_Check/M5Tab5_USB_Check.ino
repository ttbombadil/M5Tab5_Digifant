#include <M5Unified.h>
#include <EspUsbHost.h>

#ifndef SIMULATION_MODE
#define SIMULATION_MODE false
#endif

EspUsbHost usbHost;
EspUsbHostCdcSerial usbSerial(usbHost);

static void printHexLine(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    Serial.printf("%02X ", data[i]);
  }
  Serial.println();
}

static void simulateKwpPacket(uint8_t scenario) {
  static uint8_t counter = 0;

  uint8_t frame[16] = {0};
  size_t len = 0;

  if (scenario == 0) {
    // Valid minimal KWP1281-like frame
    frame[len++] = 0x55;
    frame[len++] = 0x01;
    frame[len++] = 0x06;
    frame[len++] = 0x20;
    frame[len++] = 0x00;
    frame[len++] = 0x00;
    frame[len++] = 0x01;
    frame[len++] = 0x03;
    frame[len++] = 0x00;
    frame[len++] = 0x00;
    frame[len++] = 0x00;
    frame[len++] = 0x00;
    frame[len++] = 0x7A;
    frame[len++] = 0x00;
  } else if (scenario == 1) {
    // Timeout-like behavior: only header without response body
    frame[len++] = 0x55;
    frame[len++] = 0x01;
    frame[len++] = 0x00;
  } else if (scenario == 2) {
    // Incorrect checksum / CRC-like corruption
    frame[len++] = 0x55;
    frame[len++] = 0x01;
    frame[len++] = 0x04;
    frame[len++] = 0x20;
    frame[len++] = 0x01;
    frame[len++] = 0x00;
    frame[len++] = 0x00;
    frame[len++] = 0xFF;
  } else {
    // Produces a pseudo-diagnostic update with G69 and engine state variation
    frame[len++] = 0x55;
    frame[len++] = 0x01;
    frame[len++] = 0x08;
    frame[len++] = 0x20;
    frame[len++] = counter++;
    frame[len++] = 0x00;
    frame[len++] = 0x1A;
    frame[len++] = 0x32;
    frame[len++] = 0x00;
    frame[len++] = 0x00;
    frame[len++] = 0x00;
    frame[len++] = 0x00;
    frame[len++] = 0x00;
    frame[len++] = 0xA5;
  }

  Serial.println("[SIM] generated KWP1281-like packet");
  printHexLine(frame, len);
  Serial.printf("[SIM] scenario=%u len=%u\n", scenario, len);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.internal_imu = false;
  M5.begin(cfg);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("M5Tab5 USB Check");

  Serial.println("========================================");
  Serial.println("M5Tab5 USB Host + KWP1281 test firmware");
  Serial.printf("SIMULATION_MODE=%s\n", SIMULATION_MODE ? "true" : "false");
  Serial.println("========================================");

#if SIMULATION_MODE
  Serial.println("[MODE] running in simulation mode");
  Serial.println("[MODE] no physical USB device required for protocol testing");
#else
  Serial.println("[MODE] running in hardware USB-host mode");

  usbHost.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    Serial.printf("[USB] device connected: vid=0x%04X pid=0x%04X manufacturer=%s product=%s\n",
                  info.vid,
                  info.pid,
                  info.manufacturer,
                  info.product);
  });

  usbHost.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info) {
    Serial.printf("[USB] device disconnected: vid=0x%04X pid=0x%04X\n", info.vid, info.pid);
  });

  if (!usbHost.begin()) {
    Serial.println("[USB] begin() failed");
    M5.Display.println("USB init failed");
    return;
  }

  if (!usbSerial.begin(115200)) {
    Serial.println("[USB] serial begin() failed");
    M5.Display.println("Serial init failed");
    return;
  }

  Serial.println("[USB] USB host ready");
  M5.Display.println("USB host ready");
#endif
}

void loop() {
  M5.update();

#if SIMULATION_MODE
  static uint32_t lastSim = 0;
  static uint8_t scenario = 0;

  if (millis() - lastSim > 3000) {
    lastSim = millis();
    simulateKwpPacket(scenario);
    scenario = (scenario + 1) % 4;
    Serial.println("[SIM] waiting for next frame...");
  }
#else
  static uint32_t lastByteTest = 0;
  static uint32_t lastTxTime = 0;
  static bool waitingForResponse = false;
  static const uint8_t testFrame[3] = {0x55, 0x01, 0x00};

  if (usbSerial.connected()) {
    // M2: periodic raw byte communication test over the FTDI usbSerial path.
    if (!waitingForResponse && millis() - lastByteTest > 3000) {
      lastByteTest = millis();
      lastTxTime = millis();
      waitingForResponse = true;
      size_t written = usbSerial.write(testFrame, sizeof(testFrame));
      Serial.printf("[USB] tx: 55 01 00 (%u bytes written)\n", static_cast<unsigned>(written));
    }

    while (usbSerial.available() > 0) {
      int ch = usbSerial.read();
      Serial.printf("[USB] rx=%02X\n", static_cast<uint8_t>(ch));
      waitingForResponse = false;
    }

    if (waitingForResponse && millis() - lastTxTime > 1000) {
      Serial.println("[USB] no response within 1000ms (timeout)");
      waitingForResponse = false;
    }
  }

  if (usbHost.ready()) {
    Serial.println("[USB] host ready, waiting for USB serial device...");
    delay(1000);
  }
#endif

  delay(50);
}
