#pragma once

#include <stdint.h>

namespace audio_probe {

constexpr uint8_t kTestCount = 6;

enum class UiState : uint8_t { List, Detail, Capturing, StopConfirm, Result };
enum class UiEvent : uint8_t {
  None, SelectTest, Activate, Stop, Resume, ConfirmStop, CompleteCapture, Repeat, Back
};
enum class EffectRequest : uint8_t { None, StartMicrophone, StopMicrophone };

struct UiModel {
  UiState state = UiState::List;
  uint8_t selectedTest = 0;
};

struct TransitionResult { bool accepted = false; bool changed = false; };

inline const char* stateName(UiState state) {
  switch (state) {
    case UiState::List: return "LIST";
    case UiState::Detail: return "DETAIL";
    case UiState::Capturing: return "CAPTURING_SIM";
    case UiState::StopConfirm: return "STOP_CONFIRM";
    case UiState::Result: return "RESULT_SIM";
  }
  return "UNKNOWN";
}

inline TransitionResult dispatch(UiModel& model, UiEvent event, uint8_t selectedTest = 0) {
  const UiModel before = model;
  bool accepted = false;
  switch (event) {
    case UiEvent::SelectTest:
      if (selectedTest < kTestCount && model.state != UiState::Capturing &&
          model.state != UiState::StopConfirm) {
        model.selectedTest = selectedTest;
        model.state = UiState::Detail;
        accepted = true;
      }
      break;
    case UiEvent::Activate:
      if (model.state == UiState::Detail) { model.state = UiState::Capturing; accepted = true; }
      break;
    case UiEvent::Stop:
      if (model.state == UiState::Capturing) { model.state = UiState::StopConfirm; accepted = true; }
      break;
    case UiEvent::Resume:
      if (model.state == UiState::StopConfirm) { model.state = UiState::Capturing; accepted = true; }
      break;
    case UiEvent::ConfirmStop:
      if (model.state == UiState::StopConfirm) { model.state = UiState::Result; accepted = true; }
      break;
    case UiEvent::CompleteCapture:
      if (model.state == UiState::Capturing) { model.state = UiState::Result; accepted = true; }
      break;
    case UiEvent::Repeat:
      if (model.state == UiState::Result) { model.state = UiState::Detail; accepted = true; }
      break;
    case UiEvent::Back:
      if (model.state == UiState::Detail || model.state == UiState::Result) {
        model.state = UiState::List;
        accepted = true;
      }
      break;
    case UiEvent::None:
      break;
  }
  return {accepted, before.state != model.state || before.selectedTest != model.selectedTest};
}

inline EffectRequest effectForTransition(const UiModel& before,
                                         const UiModel& after) {
  if (before.state != UiState::Capturing && after.state == UiState::Capturing) {
    return EffectRequest::StartMicrophone;
  }
  if (before.state == UiState::Capturing && after.state != UiState::Capturing) {
    return EffectRequest::StopMicrophone;
  }
  return EffectRequest::None;
}

inline UiEvent eventForTap(const UiModel& model, uint8_t row, uint8_t zone) {
  if (row >= kTestCount || zone > 2) return UiEvent::None;
  if (model.state == UiState::List) return UiEvent::SelectTest;
  if (row != model.selectedTest) {
    return model.state == UiState::Detail || model.state == UiState::Result
             ? UiEvent::SelectTest : UiEvent::None;
  }
  switch (model.state) {
    case UiState::Detail: return zone == 0 ? UiEvent::Back : UiEvent::Activate;
    case UiState::Capturing: return UiEvent::Stop;
    case UiState::StopConfirm: return zone == 0 ? UiEvent::Resume : UiEvent::ConfirmStop;
    case UiState::Result: return zone == 1 ? UiEvent::Repeat : UiEvent::Back;
    case UiState::List: return UiEvent::SelectTest;
  }
  return UiEvent::None;
}

}  // namespace audio_probe
