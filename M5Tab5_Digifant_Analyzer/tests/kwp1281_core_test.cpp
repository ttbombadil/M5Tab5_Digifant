#include "../src/kwp1281_core.h"
#include <cassert>

using namespace digifant::kwp;

int main() {
  KwpProtocolCore core;
  core.connected(7, 1000);
  core.advance(2'601'000);
  KwpAction action{};
  assert(core.popAction(action) && action.kind == ActionKind::SetBaud && action.value == 1200);
  core.completion(action.token, CompletionStatus::Completed);
  core.advance(2'601'000);
  assert(core.popAction(action) && action.kind == ActionKind::SetBreak && action.value == 1);
  core.completion(action.token, CompletionStatus::Completed);
  core.advance(2'801'000);
  assert(core.popAction(action) && action.kind == ActionKind::SetBreak && action.value == 0);
  core.completion(action.token, CompletionStatus::Completed);
  core.advance(3'001'000);
  assert(core.popAction(action) && action.kind == ActionKind::SetBreak && action.value == 1);
  core.completion(action.token, CompletionStatus::Completed);
  core.advance(3'201'000);
  assert(!core.popAction(action));
  core.advance(3'401'000);
  assert(!core.popAction(action));
  core.advance(3'601'000);
  assert(!core.popAction(action));
  core.advance(3'801'000);
  assert(!core.popAction(action));
  core.advance(4'001'000);
  assert(!core.popAction(action));
  core.advance(4'201'000);
  assert(!core.popAction(action));
  core.advance(4'401'000);
  assert(core.popAction(action) && action.kind == ActionKind::SetBreak && action.value == 0);
  core.completion(action.token, CompletionStatus::Completed);
  core.advance(4'601'000);
  assert(core.state() == CoreState::WaitSync);
  core.rxByte(0x55, 4'801'100);
  core.rxByte(0x08, 4'801'200);
  core.rxByte(0x55, 4'801'300);
  core.advance(4'826'300);
  assert(core.popAction(action) && action.kind == ActionKind::SendByte && action.value == 0xAA);
  // The ECU echo may arrive before the USB completion.
  core.rxByte(0xAA, 4'826'400);
  core.completion(action.token, CompletionStatus::Completed);
  assert(core.state() == CoreState::Active);

  KwpProtocolCore completion_first;
  completion_first.connected(8, 1000);
  completion_first.advance(2'601'000);
  assert(completion_first.popAction(action));
  completion_first.completion(action.token, CompletionStatus::Completed);
  completion_first.advance(2'601'000);
  assert(completion_first.popAction(action) && action.kind == ActionKind::SetBreak);
  completion_first.completion(action.token, CompletionStatus::Completed);
  const uint64_t wave_times[] = {2'801'000, 3'001'000, 3'201'000, 3'401'000,
                                 3'601'000, 3'801'000, 4'001'000, 4'201'000, 4'401'000};
  for (const auto now : wave_times) {
    completion_first.advance(now);
    if (completion_first.popAction(action))
      completion_first.completion(action.token, CompletionStatus::Completed);
  }
  completion_first.advance(4'601'000);
  completion_first.rxByte(0x55, 4'801'100);
  completion_first.rxByte(0x08, 4'801'200);
  completion_first.rxByte(0x55, 4'801'300);
  completion_first.advance(4'826'300);
  assert(completion_first.popAction(action) && action.kind == ActionKind::SendByte);
  completion_first.completion(action.token, CompletionStatus::Completed);
  assert(completion_first.state() == CoreState::KeyAck);
  completion_first.advance(4'826'500);
  assert(!completion_first.popAction(action));
  completion_first.rxByte(0xAA, 4'826'400);
  assert(completion_first.state() == CoreState::Active);
  return 0;
}
