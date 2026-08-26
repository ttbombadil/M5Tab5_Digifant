#include "../src/kwp1281_core.h"

#include <cassert>

using namespace digifant::kwp;

int main() {
  // Product contract: KwpProtocolCore ignores completion tokens from a
  // different transport generation/session instead of advancing state.
  KwpProtocolCore core;
  core.connected(7, 1000);
  core.advance(2'601'000);
  KwpAction action{};
  assert(core.popAction(action));
  const auto before = core.state();
  auto stale = action.token;
  stale.transportGeneration = 6;
  core.completion(stale, CompletionStatus::Completed);
  assert(core.state() == before);
  core.completion(action.token, CompletionStatus::Completed);
  core.advance(2'601'000);
  assert(core.popAction(action));
  return 0;
}
