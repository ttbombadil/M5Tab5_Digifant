#include "../src/critical_transport_event_ring.h"

#include <cassert>

using digifant::transport::CriticalTransportEvent;
using digifant::transport::CriticalTransportEventKind;
using digifant::transport::CriticalTransportEventRing;

int main() {
  CriticalTransportEventRing ring;
  CriticalTransportEvent event{};
  for (uint16_t i = 0; i < CriticalTransportEventRing::kUsableSlots; ++i) {
    event.sequence = i;
    assert(ring.tryPush(event));
  }
  assert(!ring.tryPush(event));
  assert(ring.overflowSticky());
  assert(ring.drops() == 1);
  for (uint16_t i = 0; i < CriticalTransportEventRing::kUsableSlots; ++i) {
    assert(ring.tryPop(event));
    assert(event.sequence == i);
  }
  assert(!ring.tryPop(event));

  event = CriticalTransportEvent{CriticalTransportEventKind::Disconnect, 0, 4, 9, 0, 0, 99, 1000};
  assert(ring.tryPush(event));
  assert(ring.tryPop(event));
  assert(event.kind == CriticalTransportEventKind::Disconnect);
  assert(event.generation == 9 && event.sequence == 99);
  return 0;
}
