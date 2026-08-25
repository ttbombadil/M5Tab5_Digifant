#include "../src/transport_operation_lifecycle.h"

#include <cassert>

int main()
{
  using namespace digifant::transport;
  OperationLifecycle op;
  const OperationToken first{7, 1, 1};
  const OperationToken stale{7, 2, 1};
  assert(op.accept(first));
  assert(op.state() == OperationState::Active);
  assert(!op.accept(stale));
  assert(!op.terminal(stale, OperationStatus::Success));
  assert(op.timeout());
  assert(op.state() == OperationState::Retiring);
  assert(op.terminal(first, OperationStatus::Aborted));
  assert(op.quiescent());
  assert(op.status() == OperationStatus::Aborted);
  assert(!op.terminal(first, OperationStatus::Success));
  assert(!op.accept(first));

  const OperationToken second{8, 3, 2};
  assert(op.accept(second));
  assert(!op.terminal(first, OperationStatus::Success));
  assert(op.terminal(second, OperationStatus::Success));
  assert(op.status() == OperationStatus::Success);
  return 0;
}
