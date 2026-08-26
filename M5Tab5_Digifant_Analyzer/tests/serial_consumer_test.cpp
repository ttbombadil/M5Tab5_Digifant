#include "../src/serial_consumer.h"

#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

class FakeSerial {
 public:
  int available() const noexcept { return static_cast<int>(input.size() - readIndex); }

  int read() noexcept {
    if (readIndex == input.size()) return -1;
    return static_cast<unsigned char>(input[readIndex++]);
  }

  void printf(const char* format, ...) noexcept {
    char buffer[1024]{};
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (written > 0) output.append(buffer, static_cast<std::size_t>(written));
  }

  void println(const char* value) noexcept {
    output += value;
    output += '\n';
  }

  std::string input;
  std::string output;
  std::size_t readIndex = 0;
};

uint64_t fixedNow(void*) noexcept { return 4242; }
uint32_t fixedStack(void*) noexcept { return 77; }

}  // namespace

int main() {
  digifant::logging::LoggerCommandQueue commands;
  digifant::logging::LoggerStatusMailbox loggerStatus;
  digifant::ui::LatestSnapshotMailbox snapshots;
  digifant::imu::ImuDiagnosticsMailbox imuDiagnostics;
  std::atomic<uint8_t> tabRequest{255};
  FakeSerial serial;
  digifant::serial::SerialConsumer<FakeSerial> consumer(
      commands, loggerStatus, snapshots, imuDiagnostics, tabRequest, serial);

  serial.input = "LOG_START\nSPROTZ_START\nSPROTZ_STOP\nLOG_STOP\nMARKER\n"
                 "TAB 2\nSTATUS\nSD_STATUS\nUNKNOWN\n";
  consumer.poll(fixedNow, nullptr);
  assert(serial.output.find("SERIAL_CMD LOG_START QUEUED ts=4242\n") != std::string::npos);
  assert(serial.output.find("SERIAL_CMD SPROTZ_START QUEUED ts=4242\n") != std::string::npos);
  assert(serial.output.find("SERIAL_CMD SPROTZ_STOP QUEUED ts=4242\n") != std::string::npos);
  assert(serial.output.find("SERIAL_CMD LOG_STOP QUEUED ts=4242\n") != std::string::npos);
  assert(serial.output.find("SERIAL_CMD MARKER QUEUED ts=4242\n") != std::string::npos);
  assert(serial.output.find("SERIAL_CMD TAB 2 QUEUED\n") != std::string::npos);
  assert(serial.output.find("SD_STATUS WAITING_FOR_LOGGER_STATUS\n") != std::string::npos);
  assert(serial.output.find("SERIAL_CMD UNKNOWN=UNKNOWN\n") != std::string::npos);
  assert(tabRequest.load(std::memory_order_acquire) == 2);

  digifant::logging::LoggerCommand command{};
  assert(commands.tryReceive(command) && command.kind == digifant::logging::LoggerCommandKind::LogStart);
  assert(command.timestampUs == 4242);
  assert(commands.tryReceive(command) && command.kind == digifant::logging::LoggerCommandKind::SprotzStart);
  assert(commands.tryReceive(command) && command.kind == digifant::logging::LoggerCommandKind::SprotzStop);
  assert(commands.tryReceive(command) && command.kind == digifant::logging::LoggerCommandKind::LogStop);
  assert(commands.tryReceive(command) && command.kind == digifant::logging::LoggerCommandKind::Marker);

  digifant::logging::LoggerStatus status{};
  status.state = digifant::logging::LoggerState::Recording;
  status.lastError = digifant::logging::LoggerError::None;
  status.snapshotsWritten = 12;
  status.eventsWritten = 3;
  status.imuSamplesMerged = 125;
  status.storagePresent = true;
  std::strcpy(status.fileName.data(), "/sprotz/test.dlog");
  loggerStatus.publish(status);
  consumer.poll(fixedNow, nullptr, fixedStack, nullptr);
  assert(serial.output.find("SPROTZ_LOGGER state=2 error=0 sprotz_active=0 records=12 events=3") != std::string::npos);
  assert(serial.output.find("storage_present=1 file=/sprotz/test.dlog\n") != std::string::npos);
  assert(serial.output.find("SPROTZ_STACK_FREE_WORDS=77\n") != std::string::npos);

  digifant::ui::MeasurementSnapshot snapshot{};
  snapshot.transportGeneration = 3;
  snapshot.sessionEpoch = 4;
  snapshot.lastRxSequence = 22;
  snapshot.rpm = 950;
  snapshot.k409Connected = true;
  snapshot.kwpConnected = true;
  snapshot.ecuDataValid = true;
  snapshot.validity = digifant::ui::SignalValidity::Valid;
  snapshot.fieldCount = 1;
  snapshot.fields[0].group = 0;
  snapshot.fields[0].zone = 0;
  snapshot.fields[0].raw = 128;
  snapshot.fields[0].decodedValue = 12.5F;
  snapshots.publish(snapshot);
  consumer.poll(fixedNow, nullptr);
  assert(serial.output.find("KWP_SNAPSHOT generation=3 session=4 seq=22 rpm=950") != std::string::npos);
  assert(serial.output.find("KWP_FIELD group=0 zone=0 raw=128") != std::string::npos);

  for (uint32_t i = 0; i < 125; ++i) {
    digifant::imu::ImuSample sample{};
    sample.sequence = i;
    sample.timestampUs = 1000 + i;
    sample.validity = 7;
    imuDiagnostics.publish(sample);
  }
  consumer.poll(fixedNow, nullptr);
  assert(serial.output.find("IMU_STATUS samples=125 seq=124 ts=1124") != std::string::npos);

  serial.input.assign(32, 'X');
  serial.input.push_back('\n');
  serial.readIndex = 0;
  consumer.poll(fixedNow, nullptr);
  assert(serial.output.find("SERIAL_CMD OVERLONG\n") != std::string::npos);

  digifant::runtime::RuntimeDebug debug;
  FakeSerial debugSerial;
  std::atomic<uint8_t> debugTabRequest{255};
  digifant::serial::SerialConsumer<FakeSerial> debugConsumer(
      commands, loggerStatus, snapshots, imuDiagnostics, debugTabRequest, debugSerial, &debug);
  debugSerial.input = "TAB 3\nDEBUG_STATUS\n";
  debugConsumer.poll(fixedNow, nullptr);
  assert(debugTabRequest.load(std::memory_order_acquire) == 3);
  assert(debug.serialPolls() == 1);
  assert(debug.serialRxBytes() == debugSerial.input.size());
  assert(debug.serialCommands() == 2);
  assert(debug.serialResponsesStarted() == 2);
  assert(debug.serialResponsesCompleted() == 2);
  assert(debugSerial.output.find("DEBUG_TASK name=display") != std::string::npos);
  assert(debugSerial.output.find("DEBUG_SERIAL polls=1 rx_bytes=19 commands=2") !=
         std::string::npos);
  return 0;
}
