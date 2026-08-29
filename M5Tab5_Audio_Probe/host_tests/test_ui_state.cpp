#include "../ui_state.h"

#include <cassert>

using namespace audio_probe;

int main() {
  UiModel model;
  auto result = dispatch(model, UiEvent::SelectTest, 0);
  assert(result.accepted && result.changed && model.state == UiState::Detail);
  result = dispatch(model, UiEvent::Activate);
  assert(result.accepted && model.state == UiState::Capturing);
  result = dispatch(model, UiEvent::SelectTest, 2);
  assert(!result.accepted && !result.changed && model.selectedTest == 0);
  result = dispatch(model, UiEvent::Stop);
  assert(result.accepted && model.state == UiState::StopConfirm);
  result = dispatch(model, UiEvent::Resume);
  assert(result.accepted && model.state == UiState::Capturing);
  dispatch(model, UiEvent::Stop);
  result = dispatch(model, UiEvent::ConfirmStop);
  assert(result.accepted && model.state == UiState::Result);
  result = dispatch(model, UiEvent::Repeat);
  assert(result.accepted && model.state == UiState::Detail);
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
  return 0;
}
