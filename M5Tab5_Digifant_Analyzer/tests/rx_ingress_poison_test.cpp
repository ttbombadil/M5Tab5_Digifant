#include "../src/rx_ingress_ring.h"

#include <cassert>
#include <cstdint>

using digifant::transport::RxIngressItem;
using digifant::transport::RxIngressRing;

int main() {
  RxIngressRing ring;
  uint8_t bytes[RxIngressRing::kUsableSlots];
  for (uint16_t i = 0; i < RxIngressRing::kUsableSlots; ++i) bytes[i] = static_cast<uint8_t>(i);
  assert(ring.publishBatch(bytes, RxIngressRing::kUsableSlots, 100, 7, 1000) == RxIngressRing::kUsableSlots);
  assert(ring.state() == RxIngressRing::State::Open);
  uint16_t value = 0;
  RxIngressItem item;
  while (ring.tryPop(item)) { assert(item.byte == static_cast<uint8_t>(value++)); assert(item.ingressEpoch == 1); }
  assert(value == RxIngressRing::kUsableSlots);

  uint8_t overflow[2] = {0xA1, 0xA2};
  assert(ring.publishBatch(bytes, RxIngressRing::kUsableSlots, 200, 7, 2000) == RxIngressRing::kUsableSlots);
  assert(ring.publishBatch(overflow, 2, 201, 7, 2511) == 0);
  assert(ring.state() == RxIngressRing::State::Poisoned);
  assert(ring.overflowSticky());
  assert(ring.dropped() == 2);
  assert(ring.publishBatch(overflow, 2, 202, 7, 2513) == 0);
  assert(ring.dropped() == 4);
  while (ring.tryPop(item)) {}
  assert(ring.resetAfterQuiescence());
  assert(ring.state() == RxIngressRing::State::Open);
  assert(ring.epoch() == 2);
  assert(ring.publishBatch(overflow, 2, 300, 8, 3000) == 2);
  assert(ring.tryPop(item));
  assert(item.ingressEpoch == 2 && item.transportGeneration == 8 && item.transportEventSequence == 3000);
  assert(ring.tryPop(item));
  assert(item.transportEventSequence == 3001);
  assert(!ring.tryPop(item));

  // A reset attempted from inside an active producer callback is rejected.
  assert(ring.beginProducerBatch());
  for (uint16_t i = 0; i < RxIngressRing::kUsableSlots; ++i)
    assert(ring.publishOne(static_cast<uint8_t>(i), 400, 9, 4000 + i));
  assert(!ring.publishOne(0xFF, 400, 9, 4511));
  assert(ring.state() == RxIngressRing::State::Poisoned);
  assert(!ring.resetAfterQuiescence());
  ring.endProducerBatch();
  while (ring.tryPop(item)) {}
  assert(ring.resetAfterQuiescence());
  return 0;
}
