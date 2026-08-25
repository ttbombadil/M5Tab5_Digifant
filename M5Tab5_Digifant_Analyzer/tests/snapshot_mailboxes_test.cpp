#include "../src/measurement_snapshot.h"

#include <cassert>

int main() {
  digifant::ui::LatestSnapshotMailbox serial;
  digifant::ui::LatestSnapshotMailbox display;
  digifant::ui::MeasurementSnapshot first{};
  first.sessionEpoch = 4;
  first.transportGeneration = 8;
  first.rpm = 900;
  serial.publish(first);
  display.publish(first);

  digifant::ui::MeasurementSnapshot second = first;
  second.rpm = 1200;
  display.publish(second);

  digifant::ui::MeasurementSnapshot received{};
  assert(serial.receive(received) && received.rpm == 900);
  assert(display.receive(received) && received.rpm == 1200);

  for (uint16_t i = 0; i < 10; ++i) {
    second.rpm = static_cast<uint16_t>(1300 + i);
    serial.publish(second);
  }
  assert(serial.receive(received) && received.rpm == 1309);
  assert(serial.overwrites() == 1);
  return 0;
}
