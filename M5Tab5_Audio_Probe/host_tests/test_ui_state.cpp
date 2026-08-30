#include "../ui_state.h"

#include <cassert>

using namespace audio_probe;

int main() {
  UiModel model;
  auto result = dispatch(model, UiEvent::SelectTest, 0);
  assert(result.accepted && result.changed && model.state == UiState::Detail);
  result = dispatch(model, UiEvent::Activate);
  assert(result.accepted && model.state == UiState::Capturing);
  UiModel detail{UiState::Detail, 0};
  assert(effectForTransition(detail, model) == EffectRequest::StartMicrophone);
  result = dispatch(model, UiEvent::SelectTest, 2);
  assert(!result.accepted && !result.changed && model.selectedTest == 0);
  UiModel capturing = model;
  result = dispatch(model, UiEvent::Stop);
  assert(result.accepted && model.state == UiState::StopConfirm);
  assert(effectForTransition(capturing, model) == EffectRequest::StopMicrophone);
  result = dispatch(model, UiEvent::Resume);
  assert(result.accepted && model.state == UiState::Capturing);
  assert(effectForTransition(UiModel{UiState::StopConfirm, 0}, model) ==
         EffectRequest::StartMicrophone);
  UiModel automaticCapture{UiState::Capturing, 0};
  UiModel automaticResult = automaticCapture;
  result = dispatch(automaticResult, UiEvent::CompleteCapture);
  assert(result.accepted && automaticResult.state == UiState::Result);
  assert(effectForTransition(automaticCapture, automaticResult) ==
         EffectRequest::StopMicrophone);
  dispatch(model, UiEvent::Stop);
  result = dispatch(model, UiEvent::ConfirmStop);
  assert(result.accepted && model.state == UiState::Result);
  result = dispatch(model, UiEvent::Repeat);
  assert(result.accepted && model.state == UiState::Detail);
  assert(effectForTransition(UiModel{UiState::Result, 0}, model) ==
         EffectRequest::None);
  result = dispatch(model, UiEvent::Back);
  assert(result.accepted && model.state == UiState::List);

  assert(eventForTap(model, 3, 1) == UiEvent::SelectTest);
  dispatch(model, UiEvent::SelectTest, 3);
  assert(eventForTap(model, 3, 0) == UiEvent::Back);
  assert(eventForTap(model, 3, 1) == UiEvent::Activate);
  assert(eventForTap(model, 4, 1) == UiEvent::SelectTest);
  dispatch(model, UiEvent::Activate);
  assert(eventForTap(model, 3, 0) == UiEvent::Stop);
  assert(eventForTap(model, 4, 1) == UiEvent::None);
  dispatch(model, UiEvent::Stop);
  assert(eventForTap(model, 3, 0) == UiEvent::Resume);
  assert(eventForTap(model, 3, 1) == UiEvent::ConfirmStop);
  dispatch(model, UiEvent::ConfirmStop);
  assert(eventForTap(model, 3, 0) == UiEvent::Back);
  assert(eventForTap(model, 3, 1) == UiEvent::Repeat);
  assert(eventForTap(model, 3, 2) == UiEvent::WriteWav);
  assert(effectForEvent(UiEvent::WriteWav, model) == EffectRequest::WriteWav);
  return 0;
}
