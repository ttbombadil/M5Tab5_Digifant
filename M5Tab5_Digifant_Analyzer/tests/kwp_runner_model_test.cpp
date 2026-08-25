#include "../src/kwp_runner_model.h"
#include <cassert>

using namespace digifant::kwp;

int main() {
  KwpRunnerModel runner;
  runner.handle(RunnerEvent{RunnerEventKind::Connected, 1000, 4});
  runner.handle(RunnerEvent{RunnerEventKind::Advance, 2'601'000});
  KwpAction action{};
  assert(runner.popAction(action) && action.kind == ActionKind::SetBaud);
  runner.handle(RunnerEvent{RunnerEventKind::Completion, 2'601'100, 4, action.token});
  runner.handle(RunnerEvent{RunnerEventKind::Advance, 2'601'100});
  assert(runner.popAction(action) && action.kind == ActionKind::SetBreak);
  const KwpOpToken stale = action.token;
  runner.handle(RunnerEvent{RunnerEventKind::Completion, 2'601'200, 4,
                            KwpOpToken{99, stale.sessionEpoch, stale.semanticTurnId, stale.transportOpId, stale.operationKind}});
  runner.handle(RunnerEvent{RunnerEventKind::Completion, 2'601'300, 4, stale});
  assert(runner.state() == CoreState::FiveBaud);
  runner.handle(RunnerEvent{RunnerEventKind::Disconnected, 2'601'400, 4});
  assert(runner.state() == CoreState::Disconnected);
  return 0;
}
