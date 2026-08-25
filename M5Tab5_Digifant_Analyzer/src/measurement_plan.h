#pragma once

#include <cstdint>

#include "kwp_application_parser.h"

namespace digifant::application {

enum class PlanStage : uint8_t {
  WaitingIdentification,
  WaitingGroupZero,
  WaitingGroupSwitchAck,
  WaitingGroupHeader,
  WaitingGroupBody,
  Fault
};

enum class PlanCommandKind : uint8_t { Ack, GroupZero, GroupSelect };

struct PlanCommand {
  PlanCommandKind kind{};
  uint8_t group = 0;
};

class MeasurementPlan {
 public:
  void start() noexcept {
    stage_ = PlanStage::WaitingIdentification;
    group_ = 0;
    pending_ = false;
  }

  bool onFrame(const domain::ParsedFrame& frame) noexcept {
    if (!frame.valid || pending_ || stage_ == PlanStage::Fault) {
      if (pending_) stage_ = PlanStage::Fault;
      return false;
    }

    switch (stage_) {
      case PlanStage::WaitingIdentification:
        if (frame.title == domain::ParsedTitle::Identification) {
          return emit(PlanCommand{PlanCommandKind::Ack, 0});
        }
        if (frame.title == domain::ParsedTitle::Ack) {
          group_ = 0;
          stage_ = PlanStage::WaitingGroupZero;
          return emit(PlanCommand{PlanCommandKind::GroupZero, 0});
        }
        return false;

      case PlanStage::WaitingGroupZero:
        if (frame.title != domain::ParsedTitle::GroupBody) return false;
        group_ = 1;
        stage_ = PlanStage::WaitingGroupSwitchAck;
        return emit(PlanCommand{PlanCommandKind::Ack, 0});

      case PlanStage::WaitingGroupSwitchAck:
        if (frame.title != domain::ParsedTitle::Ack) return false;
        stage_ = PlanStage::WaitingGroupHeader;
        return emit(PlanCommand{PlanCommandKind::GroupSelect, group_});

      case PlanStage::WaitingGroupHeader:
        if (frame.title != domain::ParsedTitle::GroupHeader) return false;
        stage_ = PlanStage::WaitingGroupBody;
        return emit(PlanCommand{PlanCommandKind::GroupSelect, group_});

      case PlanStage::WaitingGroupBody:
        if (frame.title == domain::ParsedTitle::Refused) {
          return advanceAfterGroup();
        }
        if (frame.title != domain::ParsedTitle::GroupBody) return false;
        return advanceAfterGroup();

      case PlanStage::Fault:
        return false;
    }
    return false;
  }

  bool popCommand(PlanCommand& command) noexcept {
    if (!pending_) return false;
    command = pendingCommand_;
    pending_ = false;
    return true;
  }

  PlanStage stage() const noexcept { return stage_; }
  uint8_t group() const noexcept { return group_; }

 private:
  bool emit(PlanCommand command) noexcept {
    pendingCommand_ = command;
    pending_ = true;
    return true;
  }

  bool advanceAfterGroup() noexcept {
    if (group_ >= 4) {
      group_ = 0;
      stage_ = PlanStage::WaitingGroupZero;
      return emit(PlanCommand{PlanCommandKind::GroupZero, 0});
    }
    ++group_;
    stage_ = PlanStage::WaitingGroupSwitchAck;
    return emit(PlanCommand{PlanCommandKind::Ack, 0});
  }

  PlanStage stage_ = PlanStage::Fault;
  PlanCommand pendingCommand_{};
  bool pending_ = false;
  uint8_t group_ = 0;
};

}  // namespace digifant::application
