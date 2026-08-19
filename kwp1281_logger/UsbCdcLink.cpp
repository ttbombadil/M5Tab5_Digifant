#include "UsbCdcLink.h"

#include "Console.h"

UsbCdcLink::UsbCdcLink() : _serial(_host) {}

bool UsbCdcLink::begin(uint32_t baud) {
  _host.onDeviceConnected([](const EspUsbHostDeviceInfo &info) {
    console.printf("[USB] device connected: vid=0x%04X pid=0x%04X", info.vid,
                    info.pid);
  });

  _host.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info) {
    console.printf("[USB] device disconnected: vid=0x%04X pid=0x%04X",
                    info.vid, info.pid);
  });

  if (!_host.begin()) {
    console.println("[USB] host begin() failed");
    return false;
  }

  if (!_serial.begin(baud)) {
    console.println("[USB] serial begin() failed");
    return false;
  }

  return true;
}

bool UsbCdcLink::isConnected() { return _serial.connected(); }

int UsbCdcLink::available() { return _serial.available(); }

int UsbCdcLink::read() { return _serial.read(); }

size_t UsbCdcLink::write(const uint8_t *buffer, size_t size) {
  return _serial.write(buffer, size);
}

bool UsbCdcLink::hostReady() { return _host.ready(); }

bool UsbCdcLink::setBaudRate(uint32_t baud) { return _serial.setBaudRate(baud); }

bool UsbCdcLink::setDtr(bool enable) { return _serial.setDtr(enable); }

bool UsbCdcLink::setRts(bool enable) { return _serial.setRts(enable); }

bool UsbCdcLink::setBreak(bool enable) {
  // FTDI SIO_SET_DATA request 0x04:
  // Bit 14 (0x4000) controls BREAK state: 1 = BREAK ON (TX line LOW/Spacing), 0 = BREAK OFF (TX line HIGH/Marking)
  // RequestType 0x40 (VENDOR_OUT_REQUEST_TYPE), wValue: 0x4000 (Break on) or 0x0008 (Break off, 8N1)
  const uint16_t value = enable ? 0x4000 : 0x0008; // 0x0008 = 8 data bits, no parity, 1 stop bit
  return _host.submitVendorSerialControl(0x40, 0x04, value, 0x0001, nullptr, 0, _serial.address());
}

bool UsbCdcLink::setLatencyTimer(uint8_t latencyMs) {
  // FTDI SIO_SET_LATENCY_TIMER request 0x09: wValue = latency in milliseconds (e.g. 1ms)
  return _host.submitVendorSerialControl(0x40, 0x09, latencyMs, 0x0001, nullptr, 0, _serial.address());
}
