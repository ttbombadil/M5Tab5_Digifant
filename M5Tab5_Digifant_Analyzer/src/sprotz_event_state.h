#pragma once

#include "logger_types.h"

#include <cstdint>

namespace digifant::logging {

enum class SprotzEventAction : uint8_t {
  Ignore,
  LogStart,
  LogStop,
  EventStart,
  EventStop,
  Marker
};

struct SprotzEventTransition {
  SprotzEventAction action = SprotzEventAction::Ignore;
  bool closeActiveEvent = false;
};

// Logger-task-owned state machine for the user-visible Sprotz event pair.
// It emits no bytes and owns no queue; the logger core commits a transition
// only after the corresponding existing DLOG record was written.
class SprotzEventStateMachine {
 public:
  SprotzEventTransition preview(LoggerCommandKind command, bool logging) const noexcept {
    switch (command) {
      case LoggerCommandKind::LogStart:
        return logging ? SprotzEventTransition{} :
                         SprotzEventTransition{SprotzEventAction::LogStart, false};
      case LoggerCommandKind::LogStop:
        return logging ? SprotzEventTransition{SprotzEventAction::LogStop, active_} :
                         SprotzEventTransition{};
      case LoggerCommandKind::SprotzStart:
        return logging && !active_ ?
                   SprotzEventTransition{SprotzEventAction::EventStart, false} :
                   SprotzEventTransition{};
      case LoggerCommandKind::SprotzStop:
        return logging && active_ ?
                   SprotzEventTransition{SprotzEventAction::EventStop, false} :
                   SprotzEventTransition{};
      case LoggerCommandKind::Marker:
        return logging ? SprotzEventTransition{SprotzEventAction::Marker, false} :
                         SprotzEventTransition{};
    }
    return {};
  }

  void commit(SprotzEventAction action) noexcept {
    if (action == SprotzEventAction::LogStart || action == SprotzEventAction::LogStop)
      active_ = false;
    else if (action == SprotzEventAction::EventStart)
      active_ = true;
    else if (action == SprotzEventAction::EventStop)
      active_ = false;
  }

  void reset() noexcept { active_ = false; }
  bool active() const noexcept { return active_; }

 private:
  bool active_ = false;
};

}  // namespace digifant::logging
