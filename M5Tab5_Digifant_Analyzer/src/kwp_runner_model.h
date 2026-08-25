#pragma once

#include "kwp1281_core.h"
#include "kwp_receive_service.h"

namespace digifant::kwp {

enum class RunnerEventKind : uint8_t { Connected, Disconnected, RxByte, Completion, Advance };
struct RunnerEvent {
  RunnerEventKind kind = RunnerEventKind::Advance;
  uint64_t atUs = 0;
  uint32_t generation = 0;
  KwpOpToken token{};
  CompletionStatus completion = CompletionStatus::Completed;
  uint8_t byte = 0;
};

class KwpRunnerModel {
 public:
  void handle(const RunnerEvent& event) noexcept {
    switch (event.kind) {
      case RunnerEventKind::Connected: core_.connected(event.generation, event.atUs); break;
      case RunnerEventKind::Disconnected: core_.disconnected(); break;
      case RunnerEventKind::RxByte: {
        transport::RxIngressItem item{};
        item.byte = event.byte;
        item.batchTimestampUs = static_cast<uint32_t>(event.atUs);
        item.transportGeneration = event.generation;
        item.ingressEpoch = ingressEpoch_;
        item.transportEventSequence = ++sequence_;
        receive_.consume(item);
        core_.rxByte(event.byte, event.atUs);
        break;
      }
      case RunnerEventKind::Completion: core_.completion(event.token, event.completion); break;
      case RunnerEventKind::Advance: core_.advance(event.atUs); break;
    }
    drainCoreActions();
  }

  bool popAction(KwpAction& action) noexcept {
    if (actionRead_ == actionCount_) return false;
    action = actions_[actionRead_++];
    return true;
  }
  CoreState state() const noexcept { return core_.state(); }
  const KwpReceiveService& receive() const noexcept { return receive_; }

 private:
  void drainCoreActions() noexcept {
    KwpAction action{};
    while (core_.popAction(action) && actionCount_ < actions_.size()) actions_[actionCount_++] = action;
  }
  KwpProtocolCore core_{};
  KwpReceiveService receive_{};
  std::array<KwpAction, 32> actions_{};
  uint8_t actionRead_ = 0, actionCount_ = 0;
  uint32_t ingressEpoch_ = 1;
  uint64_t sequence_ = 0;
};

}  // namespace digifant::kwp
