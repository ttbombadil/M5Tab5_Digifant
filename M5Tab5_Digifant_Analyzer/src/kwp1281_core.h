#pragma once

#include <array>
#include <cstdint>

namespace digifant::kwp {

struct KwpTimingProfile {
  uint64_t busIdleUs = 2'600'000;
  uint64_t fiveBaudCellUs = 200'000;
  uint64_t stopHoldUs = 200'000;
  uint64_t syncTimeoutUs = 300'000;
  uint64_t keyAckNotBeforeUs = 25'000;
  uint64_t keyAckDeadlineUs = 40'000;
  uint64_t echoTimeoutUs = 150'000;
};

struct KwpOpToken {
  uint32_t transportGeneration = 0;
  uint32_t sessionEpoch = 0;
  uint64_t semanticTurnId = 0;
  uint64_t transportOpId = 0;
  uint8_t operationKind = 0;
};

enum class ActionKind : uint8_t { SetBaud, SetBreak, SendByte };
struct KwpAction { ActionKind kind{}; uint32_t value = 0; KwpOpToken token{}; uint64_t notBeforeUs = 0; };

enum class CompletionStatus : uint8_t { Completed, Failed, Canceled, OutcomeUnknown };
enum class CoreState : uint8_t { Disconnected, BusIdle, FiveBaud, WaitSync, WaitKeyBytes, KeyAck, Active, Fault };

class KwpProtocolCore {
 public:
  explicit KwpProtocolCore(KwpTimingProfile profile = {}) : profile_(profile) {}

  void connected(uint32_t generation, uint64_t now) {
    ++sessionEpoch_;
    if (sessionEpoch_ == 0) sessionEpoch_ = 1;
    beginConnected(generation, sessionEpoch_, operationId_ + 1U, now);
  }

  void connectedWithIds(uint32_t generation, uint32_t session_epoch,
                        uint64_t first_operation_id, uint64_t now) {
    beginConnected(generation, session_epoch, first_operation_id, now);
  }

  uint64_t nextOperationId() const { return operationId_ + 1U; }

 private:
  void beginConnected(uint32_t generation, uint32_t session_epoch,
                      uint64_t first_operation_id, uint64_t now) {
    generation_ = generation;
    sessionEpoch_ = session_epoch == 0 ? 1 : session_epoch;
    operationId_ = first_operation_id == 0 ? 0 : first_operation_id - 1U;
    turnId_ = 0;
    state_ = CoreState::BusIdle;
    idleDeadline_ = now + profile_.busIdleUs;
    actionCount_ = 0;
    actionRead_ = 0;
    keyAckEchoSeen_ = false;
    keyAckActionIssued_ = false;
    keyBytesSeen_ = 0;
    active_ = false;
  }

 public:

  void disconnected() { state_ = CoreState::Disconnected; actionCount_ = 0; active_ = false; }

  void advance(uint64_t now) {
    if (state_ == CoreState::BusIdle && now >= idleDeadline_) {
      issue(ActionKind::SetBaud, 1200, now);
      state_ = CoreState::FiveBaud;
      waveCell_ = 0;
      waveLevel_ = 1;
      waveStart_ = now;
      pendingWaveStart_ = true;
    } else if (state_ == CoreState::FiveBaud && !active_ && pendingWaveStart_ && now >= waveStart_) {
      pendingWaveStart_ = false;
      issueBreakForCell(now);
    } else if (state_ == CoreState::FiveBaud && !active_ && now >= waveStart_ + (waveCell_ + 1) * profile_.fiveBaudCellUs) {
      ++waveCell_;
      if (waveCell_ >= 10) {
        state_ = CoreState::WaitSync;
        syncDeadline_ = now + profile_.syncTimeoutUs;
      } else {
        issueBreakForCell(now);
      }
    } else if (state_ == CoreState::KeyAck && !active_ && !keyAckActionIssued_ &&
               !keyAckEchoSeen_ && now >= keyAckNotBefore_) {
      issue(ActionKind::SendByte, static_cast<uint8_t>(~keyByte2_), now);
    }
    if (state_ == CoreState::WaitSync && now >= syncDeadline_) state_ = CoreState::Fault;
    if (state_ == CoreState::KeyAck && now >= keyAckDeadline_) state_ = CoreState::Fault;
  }

  void rxByte(uint8_t byte, uint64_t timestamp) {
    if (state_ == CoreState::WaitSync) {
      if (byte == 0x55) state_ = CoreState::WaitKeyBytes;
    } else if (state_ == CoreState::WaitKeyBytes) {
      if (keyBytesSeen_++ == 1) {
        keyByte2_ = byte;
        keyAckNotBefore_ = timestamp + profile_.keyAckNotBeforeUs;
        keyAckDeadline_ = timestamp + profile_.keyAckDeadlineUs;
        state_ = CoreState::KeyAck;
      }
    } else if (state_ == CoreState::KeyAck && byte == static_cast<uint8_t>(~keyByte2_)) {
      keyAckEchoSeen_ = true;
      if (active_ || keyAckActionIssued_) {
        active_ = false;
        state_ = CoreState::Active;
      }
    }
  }

  void completion(const KwpOpToken& token, CompletionStatus status) {
    if (!active_ || token.transportGeneration != activeToken_.transportGeneration ||
        token.sessionEpoch != activeToken_.sessionEpoch || token.transportOpId != activeToken_.transportOpId)
      return;
    active_ = false;
    if (status != CompletionStatus::Completed) {
      state_ = CoreState::Fault;
    } else if (state_ == CoreState::KeyAck && keyAckEchoSeen_) {
      state_ = CoreState::Active;
    }
  }

  bool popAction(KwpAction& out) {
    if (actionRead_ == actionCount_) return false;
    out = actions_[actionRead_++];
    active_ = true;
    activeToken_ = out.token;
    if (state_ == CoreState::KeyAck && out.kind == ActionKind::SendByte)
      keyAckActionIssued_ = true;
    return true;
  }
  CoreState state() const { return state_; }
  uint64_t nextWakeup() const { return state_ == CoreState::BusIdle ? idleDeadline_ :
                                      state_ == CoreState::WaitSync ? syncDeadline_ : keyAckNotBefore_; }
  uint32_t sessionEpoch() const { return sessionEpoch_; }

 private:
  void issue(ActionKind kind, uint32_t value, uint64_t now) {
    if (actionCount_ >= actions_.size()) { state_ = CoreState::Fault; return; }
    actions_[actionCount_++] = KwpAction{kind, value,
      KwpOpToken{generation_, sessionEpoch_, ++turnId_, ++operationId_, static_cast<uint8_t>(kind)}, now};
  }
  void issueBreakForCell(uint64_t now) {
    const uint8_t bits = 0x01;
    const uint8_t level = waveCell_ == 0 ? 0 : waveCell_ == 9 ? 1 : ((bits >> (waveCell_ - 1)) & 1U);
    if (level != waveLevel_ || waveCell_ == 0 || waveCell_ == 9) {
      waveLevel_ = level;
      issue(ActionKind::SetBreak, level ? 0 : 1, now);
    }
  }
  KwpTimingProfile profile_{};
  std::array<KwpAction, 16> actions_{};
  uint8_t actionCount_ = 0, actionRead_ = 0;
  CoreState state_ = CoreState::Disconnected;
  KwpOpToken activeToken_{};
  bool active_ = false, pendingWaveStart_ = false;
  uint32_t generation_ = 0, sessionEpoch_ = 0;
  uint64_t operationId_ = 0, turnId_ = 0, idleDeadline_ = 0, waveStart_ = 0;
  uint64_t syncDeadline_ = 0, keyAckNotBefore_ = 0, keyAckDeadline_ = 0;
  uint8_t waveCell_ = 0, waveLevel_ = 1, keyBytesSeen_ = 0, keyByte2_ = 0;
  bool keyAckEchoSeen_ = false;
  bool keyAckActionIssued_ = false;
};

}  // namespace digifant::kwp
