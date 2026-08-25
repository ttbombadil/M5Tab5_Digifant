#include "../src/kwp_reconnect_policy.h"

#include <cassert>
#include <cstdint>

int main() {
  using digifant::application::KwpReconnectPolicy;
  KwpReconnectPolicy policy;
  assert(!policy.shouldAttempt(false, 1, 100));
  assert(policy.shouldAttempt(true, 1, 100));
  policy.attemptFinished(100);
  assert(!policy.shouldAttempt(true, 1, 6099));
  assert(policy.shouldAttempt(true, 1, 6100));
  assert(policy.shouldAttempt(true, 2, 6101));
  policy.attemptFinished(0xFFFFFF00U);
  assert(!policy.shouldAttempt(true, 2, 0x00000020U));
  assert(policy.shouldAttempt(true, 2, 0x00002000U));
  assert(!KwpReconnectPolicy::sessionStalled(4999, 1000));
  assert(KwpReconnectPolicy::sessionStalled(5000, 1000));
  return 0;
}
