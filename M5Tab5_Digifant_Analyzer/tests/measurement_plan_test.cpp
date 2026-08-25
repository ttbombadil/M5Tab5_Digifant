#include "../src/measurement_plan.h"

#include <cassert>

using digifant::application::MeasurementPlan;
using digifant::application::PlanCommand;
using digifant::application::PlanCommandKind;
using digifant::domain::ParsedFrame;
using digifant::domain::ParsedTitle;

static ParsedFrame frame(ParsedTitle title) {
  ParsedFrame out{};
  out.valid = true;
  out.title = title;
  return out;
}

static PlanCommand take(MeasurementPlan& plan) {
  PlanCommand command{};
  assert(plan.popCommand(command));
  return command;
}

int main() {
  MeasurementPlan plan;
  plan.start();
  assert(plan.stage() == digifant::application::PlanStage::WaitingIdentification);

  assert(plan.onFrame(frame(ParsedTitle::Identification)));
  assert(take(plan).kind == PlanCommandKind::Ack);
  assert(plan.onFrame(frame(ParsedTitle::Ack)));
  assert(take(plan).kind == PlanCommandKind::GroupZero);

  assert(plan.onFrame(frame(ParsedTitle::GroupBody)));
  assert(take(plan).kind == PlanCommandKind::Ack);
  assert(plan.onFrame(frame(ParsedTitle::Ack)));
  PlanCommand command = take(plan);
  assert(command.kind == PlanCommandKind::GroupSelect && command.group == 1);
  assert(plan.onFrame(frame(ParsedTitle::GroupHeader)));
  command = take(plan);
  assert(command.kind == PlanCommandKind::GroupSelect && command.group == 1);
  assert(plan.onFrame(frame(ParsedTitle::GroupBody)));
  assert(take(plan).kind == PlanCommandKind::Ack);

  // A refused group advances without parser/UI feedback.
  assert(plan.onFrame(frame(ParsedTitle::Ack)));
  command = take(plan);
  assert(command.kind == PlanCommandKind::GroupSelect && command.group == 2);
  assert(plan.onFrame(frame(ParsedTitle::GroupHeader)));
  take(plan);
  assert(plan.onFrame(frame(ParsedTitle::Refused)));
  assert(take(plan).kind == PlanCommandKind::Ack);

  // Commands are bounded: the caller must consume one before another event.
  assert(plan.onFrame(frame(ParsedTitle::Ack)));
  assert(!plan.onFrame(frame(ParsedTitle::Ack)));
  assert(plan.stage() == digifant::application::PlanStage::Fault);
  return 0;
}
