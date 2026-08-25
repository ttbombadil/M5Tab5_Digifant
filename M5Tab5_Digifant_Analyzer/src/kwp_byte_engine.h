#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace digifant::kwp {

enum class ByteEngineState : uint8_t { Idle, HostEcho, HostAck, RxLength, RxLengthEcho, RxByte, RxByteEcho, Fault };
enum class ByteEngineFault : uint8_t { None, EchoMismatch, AckMismatch, InvalidLength, FrameOverflow, UnexpectedByte };
enum class ByteTraceDirection : uint8_t { Rx, Tx, Completion, DeferredRx, Reset, TxTokenAssigned };
struct ByteTxRequest { bool pending = false; uint8_t byte = 0; };

struct ByteEngineTraceContext {
  uint64_t timestampUs = 0;
  uint32_t transportGeneration = 0;
  uint32_t sessionEpoch = 0;
  uint64_t semanticTurnId = 0;
  uint64_t transportOperationId = 0;
  uint8_t sessionState = 0;
  uint8_t planStage = 0;
  uint8_t planGroup = 0;
  uint8_t sessionBlockCounter = 0;
};

struct ByteEngineTraceEntry {
  uint64_t timestampUs = 0;
  uint64_t semanticTurnId = 0;
  uint64_t transportOperationId = 0;
  uint32_t transportGeneration = 0;
  uint32_t sessionEpoch = 0;
  ByteTraceDirection direction = ByteTraceDirection::Rx;
  uint8_t byte = 0;
  ByteEngineState stateBefore = ByteEngineState::Idle;
  ByteEngineState stateAfter = ByteEngineState::Idle;
  uint8_t expectedFrameSize = 0;
  uint8_t frameByteIndex = 0;
  uint8_t sessionBlockCounter = 0;
  uint8_t receivedBlockCounter = 0xFF;
  uint8_t sessionState = 0;
  uint8_t planStage = 0;
  uint8_t planGroup = 0;
  ByteEngineFault fault = ByteEngineFault::None;
  bool txCompletion = false;
  bool echoSeen = false;
  bool ackSeen = false;
  bool pendingRxAfterEcho = false;
  bool frameComplete = false;
};

class KwpByteEngine {
 public:
  static constexpr uint8_t kMaxFrame = 65;
  static constexpr std::size_t kTraceCapacity = 32;

  void clearTrace() noexcept {
    traceNext_ = 0;
    traceCount_ = 0;
  }

  void setTraceContext(const ByteEngineTraceContext& context) noexcept { traceContext_ = context; }

  std::size_t traceCount() const noexcept { return traceCount_; }

  const ByteEngineTraceEntry& traceEntry(std::size_t chronological_index) const noexcept {
    const std::size_t oldest = (traceNext_ + kTraceCapacity - traceCount_) % kTraceCapacity;
    return trace_[(oldest + chronological_index) % kTraceCapacity];
  }

  void noteTxTokenAssigned(uint8_t byte) noexcept {
    const std::size_t slot = beginTrace(ByteTraceDirection::TxTokenAssigned, byte);
    finishTrace(slot);
  }

  void reset() noexcept {
    const std::size_t trace_slot = beginTrace(ByteTraceDirection::Reset, 0);
    state_ = ByteEngineState::RxLength;
    fault_ = ByteEngineFault::None;
    frameSize_ = 0;
    expectedFrameSize_ = 0;
    txCompletion_ = false;
    echoSeen_ = false;
    ackSeen_ = false;
    pendingRx_ = false;
    txRequest_ = {};
    finishTrace(trace_slot);
  }

  bool beginHostByte(uint8_t byte, bool requiresAck) noexcept {
    const std::size_t trace_slot = beginTrace(ByteTraceDirection::Tx, byte);
    if (state_ != ByteEngineState::Idle && state_ != ByteEngineState::RxLength) {
      finishTrace(trace_slot);
      return false;
    }
    expectedEcho_ = byte;
    expectedAck_ = static_cast<uint8_t>(~byte);
    requiresAck_ = requiresAck;
    txCompletion_ = false;
    echoSeen_ = false;
    ackSeen_ = false;
    txRequest_ = {true, byte};
    state_ = ByteEngineState::HostEcho;
    finishTrace(trace_slot);
    return true;
  }

  ByteTxRequest takeTxRequest() noexcept { const auto out = txRequest_; txRequest_ = {}; return out; }

  void completion(bool success) noexcept {
    const std::size_t trace_slot = beginTrace(ByteTraceDirection::Completion, expectedEcho_);
    if (state_ == ByteEngineState::HostEcho || state_ == ByteEngineState::HostAck ||
        state_ == ByteEngineState::RxLengthEcho || state_ == ByteEngineState::RxByteEcho) {
      txCompletion_ = success;
      if (!success) fail(ByteEngineFault::UnexpectedByte);
      finishIfReady();
    }
    finishTrace(trace_slot);
  }

  void onRxByte(uint8_t byte) noexcept {
    const std::size_t trace_slot = beginTrace(ByteTraceDirection::Rx, byte);
    handleRxByte(byte);
    finishTrace(trace_slot);
  }

  ByteEngineState state() const noexcept { return state_; }
  ByteEngineFault fault() const noexcept { return fault_; }
  bool frameComplete() const noexcept { return state_ == ByteEngineState::Idle && frameSize_ >= 4 && frame_[frameSize_ - 1] == 0x03; }
  const uint8_t* frameData() const noexcept { return frame_.data(); }
  uint8_t frameSize() const noexcept { return frameSize_; }

 private:
  void handleRxByte(uint8_t byte) noexcept {
    switch (state_) {
      case ByteEngineState::HostEcho:
        if (byte == expectedEcho_) {
          echoSeen_ = true;
          if (requiresAck_) state_ = ByteEngineState::HostAck;
          finishIfReady();
        } else {
          deferRxAfterEcho(byte);
        }
        return;
      case ByteEngineState::HostAck:
        if (byte == expectedAck_) { ackSeen_ = true; finishIfReady(); }
        else deferRxAfterEcho(byte);
        return;
      case ByteEngineState::RxLength:
        if (byte < 3 || byte > kMaxFrame - 1) { fail(ByteEngineFault::InvalidLength); return; }
        frame_[0] = byte;
        frameSize_ = 1;
        expectedFrameSize_ = static_cast<uint8_t>(byte + 1);
        issueInverseAck(byte, true);
        return;
      case ByteEngineState::RxLengthEcho:
        if (byte == expectedEcho_) {
          echoSeen_ = true;
          finishIfReady();
        } else {
          deferRxAfterEcho(byte);
        }
        return;
      case ByteEngineState::RxByte:
        if (frameSize_ >= expectedFrameSize_) { fail(ByteEngineFault::UnexpectedByte); return; }
        frame_[frameSize_++] = byte;
        if (frameSize_ == expectedFrameSize_) {
          if (byte != 0x03) fail(ByteEngineFault::UnexpectedByte);
          else state_ = ByteEngineState::Idle;
        } else {
          issueInverseAck(byte, false);
        }
        return;
      case ByteEngineState::RxByteEcho:
        if (byte == expectedEcho_) {
          echoSeen_ = true;
          finishIfReady();
        } else {
          deferRxAfterEcho(byte);
        }
        return;
      default: fail(ByteEngineFault::UnexpectedByte); return;
    }
  }
  void issueInverseAck(uint8_t byte, bool length) noexcept {
    const uint8_t inverse = static_cast<uint8_t>(~byte);
    const std::size_t trace_slot = beginTrace(ByteTraceDirection::Tx, inverse);
    expectedEcho_ = inverse;
    txCompletion_ = false;
    echoSeen_ = false;
    ackSeen_ = false;
    txRequest_ = {true, expectedEcho_};
    state_ = length ? ByteEngineState::RxLengthEcho : ByteEngineState::RxByteEcho;
    finishTrace(trace_slot);
  }
  void finishIfReady() noexcept {
    if (!txCompletion_ || !echoSeen_ || state_ == ByteEngineState::Fault) return;
    if (state_ == ByteEngineState::HostEcho && requiresAck_) return;
    if (state_ == ByteEngineState::HostAck) {
      if (ackSeen_) {
        // The ECU may already have started its next frame while the
        // host-ACK echo pair was being closed. Preserve that byte as the
        // next frame length instead of processing it in Idle.
        state_ = pendingRx_ ? ByteEngineState::RxLength : ByteEngineState::Idle;
        processPending();
      }
      return;
    }
    if (state_ == ByteEngineState::HostEcho && !requiresAck_) {
      state_ = pendingRx_ ? ByteEngineState::RxLength : ByteEngineState::Idle;
      processPending();
      return;
    }
    if (state_ == ByteEngineState::RxLengthEcho || state_ == ByteEngineState::RxByteEcho) {
      state_ = ByteEngineState::RxByte;
      processPending();
    }
  }
  void processPending() noexcept {
    if (!pendingRx_ || state_ == ByteEngineState::Fault) return;
    const uint8_t deferred = pendingRxByte_;
    pendingRx_ = false;
    const std::size_t trace_slot = beginTrace(ByteTraceDirection::DeferredRx, deferred);
    handleRxByte(deferred);
    finishTrace(trace_slot);
  }
  void deferRxAfterEcho(uint8_t byte) noexcept {
    if (pendingRx_) { fail(ByteEngineFault::UnexpectedByte); return; }
    pendingRx_ = true;
    pendingRxByte_ = byte;
  }
  void fail(ByteEngineFault fault) noexcept { fault_ = fault; state_ = ByteEngineState::Fault; txRequest_ = {}; }

  std::size_t beginTrace(ByteTraceDirection direction, uint8_t byte) noexcept {
    const std::size_t slot = traceNext_;
    traceNext_ = (traceNext_ + 1) % kTraceCapacity;
    if (traceCount_ < kTraceCapacity) ++traceCount_;
    auto& entry = trace_[slot];
    entry = {};
    entry.timestampUs = traceContext_.timestampUs;
    entry.semanticTurnId = traceContext_.semanticTurnId;
    entry.transportOperationId = traceContext_.transportOperationId;
    entry.transportGeneration = traceContext_.transportGeneration;
    entry.sessionEpoch = traceContext_.sessionEpoch;
    entry.direction = direction;
    entry.byte = byte;
    entry.stateBefore = state_;
    entry.stateAfter = state_;
    entry.sessionBlockCounter = traceContext_.sessionBlockCounter;
    entry.sessionState = traceContext_.sessionState;
    entry.planStage = traceContext_.planStage;
    entry.planGroup = traceContext_.planGroup;
    finishTrace(slot);
    return slot;
  }

  void finishTrace(std::size_t slot) noexcept {
    auto& entry = trace_[slot];
    entry.stateAfter = state_;
    entry.expectedFrameSize = expectedFrameSize_;
    entry.frameByteIndex = frameSize_;
    entry.receivedBlockCounter = frameSize_ > 1 ? frame_[1] : 0xFF;
    entry.fault = fault_;
    entry.txCompletion = txCompletion_;
    entry.echoSeen = echoSeen_;
    entry.ackSeen = ackSeen_;
    entry.pendingRxAfterEcho = pendingRx_;
    entry.frameComplete = frameComplete();
  }

  std::array<uint8_t, kMaxFrame> frame_{};
  ByteEngineState state_ = ByteEngineState::Idle;
  ByteEngineFault fault_ = ByteEngineFault::None;
  ByteTxRequest txRequest_{};
  uint8_t expectedEcho_ = 0, expectedAck_ = 0, frameSize_ = 0, expectedFrameSize_ = 0;
  bool requiresAck_ = false, txCompletion_ = false, echoSeen_ = false, ackSeen_ = false;
  bool pendingRx_ = false;
  uint8_t pendingRxByte_ = 0;
  std::array<ByteEngineTraceEntry, kTraceCapacity> trace_{};
  ByteEngineTraceContext traceContext_{};
  std::size_t traceNext_ = 0;
  std::size_t traceCount_ = 0;
};

}  // namespace digifant::kwp
