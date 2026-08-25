#include "../src/k409_device_filter.h"

#include <cassert>

int main()
{
  using digifant::k409::GenerationTracker;
  using digifant::k409::matches;

  assert(matches(0x0403, 0x6001));
  assert(!matches(0x0403, 0x6010));
  assert(!matches(0x1234, 0x6001));

  GenerationTracker tracker;
  assert(tracker.connected(0x1234, 0x5678) == 0);
  const auto first = tracker.connected(0x0403, 0x6001);
  const auto second = tracker.connected(0x0403, 0x6001);
  assert(first == 1);
  assert(second == 2);
  assert(tracker.current() == second);
  return 0;
}
