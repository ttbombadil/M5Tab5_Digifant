#include "../src/k409_device_filter.h"

#include <cassert>

int main()
{
  using digifant::k409::matches;

  // Product contract: the USB callbacks use this exact VID/PID predicate.
  assert(matches(0x0403, 0x6001));
  assert(!matches(0x0403, 0x6010));
  assert(!matches(0x1234, 0x6001));
  return 0;
}
