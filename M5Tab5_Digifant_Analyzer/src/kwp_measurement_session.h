#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "kwp1281_core.h"
#include "kwp_application_parser.h"
#include "kwp_byte_engine.h"
#include "measurement_plan.h"
#include "rx_ingress_ring.h"
#include "validated_frame_queue.h"

namespace digifant::application {

enum class MeasurementSessionState : uint8_t { Idle, Running, Fault };

class KwpMeasurementSession {
 public:
  void setValidatedFrameSink(transport::ValidatedFrameQueue& sink) noexcept { frame_sink_ = &sink; }

  void start(uint32_t generation, uint32_t session_epoch, uint64_t first_operation_id = 1) noexcept {
    generation_ = generation;
    session_epoch_ = session_epoch;
    next_operation_id_ = first_operation_id;
    turn_id_ = 0;
    tx_counter_ = 0;
    tx_frame_size_ = 0;
    tx_index_ = 0;
    tx_block_active_ = false;
    action_queued_ = false;
    action_issued_ = false;
    reset_after_final_echo_ = false;
    stale_completions_ = 0;
    parsed_frames_ = 0;
    identification_frames_ = 0;
    ack_frames_ = 0;
    group_header_frames_ = 0;
    group_body_frames_ = 0;
    refused_frames_ = 0;
    parser_rejected_ = 0;
    stale_rx_items_ = 0;
    ingress_epoch_ = 0;
    active_ingress_epoch_ = 0;
    plan_.start();
    state_ = MeasurementSessionState::Running;
    last_event_timestamp_us_ = 0;
    engine_.clearTrace();
    updateTraceContext(0, {});
    engine_.reset();
  }

  bool popAction(digifant::kwp::KwpAction& action) noexcept {
    if (!action_queued_) return false;
    action = queued_action_;
    action_queued_ = false;
    action_issued_ = true;
    return true;
  }

  bool onCompletion(const digifant::kwp::KwpOpToken& token,
                    digifant::kwp::CompletionStatus status,
                    uint64_t timestamp_us = 0) noexcept {
    if (!action_issued_ || token.transportGeneration != generation_ ||
        token.sessionEpoch != session_epoch_ || token.transportOpId != queued_action_.token.transportOpId) {
      ++stale_completions_;
      return false;
    }
    if (timestamp_us != 0) last_event_timestamp_us_ = timestamp_us;
    const bool final_host_byte = tx_block_active_ && tx_index_ + 1 >= tx_frame_size_;
    action_issued_ = false;
    updateTraceContext(last_event_timestamp_us_, token);
    engine_.completion(status == digifant::kwp::CompletionStatus::Completed);
    if (final_host_byte) {
      tx_block_active_ = false;
      if (engine_.state() == digifant::kwp::ByteEngineState::Idle) {
        engine_.reset();
      } else {
        reset_after_final_echo_ = true;
      }
    }
    if (engine_.fault() != digifant::kwp::ByteEngineFault::None) {
      state_ = MeasurementSessionState::Fault;
      return false;
    }
    advanceAfterTransportEvent();
    return true;
  }

  bool onRxByte(uint8_t byte, uint64_t timestamp_us) noexcept {
    if (state_ != MeasurementSessionState::Running) return false;
    last_event_timestamp_us_ = timestamp_us;
    if (engine_.frameSize() == 0) frame_first_byte_us_ = timestamp_us;
    updateTraceContext(timestamp_us, activeOperationToken());
    engine_.onRxByte(byte);
    if (reset_after_final_echo_) {
      if (engine_.state() == digifant::kwp::ByteEngineState::Idle) {
        engine_.reset();
        reset_after_final_echo_ = false;
      } else if (engine_.state() != digifant::kwp::ByteEngineState::HostEcho) {
        reset_after_final_echo_ = false;
      }
    }
    if (engine_.fault() != digifant::kwp::ByteEngineFault::None) {
      state_ = MeasurementSessionState::Fault;
      return false;
    }
    if (engine_.frameComplete()) {
      const auto parsed = domain::parseKwpFrame(engine_.frameData(), engine_.frameSize());
      if (parsed.valid) publishValidatedFrame(parsed, timestamp_us);
      engine_.reset();
      if (!parsed.valid) {
        ++parser_rejected_;
      } else {
        ++parsed_frames_;
        switch (parsed.title) {
          case domain::ParsedTitle::Identification: ++identification_frames_; break;
          case domain::ParsedTitle::Ack: ++ack_frames_; break;
          case domain::ParsedTitle::GroupHeader: ++group_header_frames_; break;
          case domain::ParsedTitle::GroupBody: ++group_body_frames_; break;
          case domain::ParsedTitle::Refused: ++refused_frames_; break;
          case domain::ParsedTitle::Unknown: break;
        }
      }
      if (!parsed.valid || !plan_.onFrame(parsed)) {
        return advanceAfterTransportEvent();
      }
      PlanCommand command{};
      if (!plan_.popCommand(command) || !buildCommand(command)) {
        state_ = MeasurementSessionState::Fault;
        return false;
      }
    }
    return advanceAfterTransportEvent();
  }

  bool onRxItem(const transport::RxIngressItem& item) noexcept {
    if (state_ != MeasurementSessionState::Running) return false;
    if (item.transportGeneration != generation_) {
      ++stale_rx_items_;
      return false;
    }
    if (ingress_epoch_ == 0) {
      ingress_epoch_ = item.ingressEpoch;
    } else if (item.ingressEpoch != ingress_epoch_) {
      state_ = MeasurementSessionState::Fault;
      return false;
    }
    active_ingress_epoch_ = item.ingressEpoch;
    return onRxByte(item.byte, item.batchTimestampUs);
  }

  void disconnect() noexcept {
    state_ = MeasurementSessionState::Fault;
    action_queued_ = false;
    action_issued_ = false;
  }

  MeasurementSessionState state() const noexcept { return state_; }
  uint8_t txCounter() const noexcept { return tx_counter_; }
  uint32_t staleCompletions() const noexcept { return stale_completions_; }
  uint32_t staleRxItems() const noexcept { return stale_rx_items_; }
  uint32_t ingressEpoch() const noexcept { return ingress_epoch_; }
  uint8_t byteEngineFault() const noexcept { return static_cast<uint8_t>(engine_.fault()); }
  uint16_t parsedFrames() const noexcept { return parsed_frames_; }
  uint16_t identificationFrames() const noexcept { return identification_frames_; }
  uint16_t ackFrames() const noexcept { return ack_frames_; }
  uint16_t groupHeaderFrames() const noexcept { return group_header_frames_; }
  uint16_t groupBodyFrames() const noexcept { return group_body_frames_; }
  uint16_t refusedFrames() const noexcept { return refused_frames_; }
  uint16_t parserRejected() const noexcept { return parser_rejected_; }
  uint64_t nextOperationId() const noexcept { return next_operation_id_; }
  std::size_t byteTraceCount() const noexcept { return engine_.traceCount(); }
  const digifant::kwp::ByteEngineTraceEntry& byteTraceEntry(std::size_t index) const noexcept {
    return engine_.traceEntry(index);
  }
  const MeasurementPlan& plan() const noexcept { return plan_; }

 private:
  void publishValidatedFrame(const domain::ParsedFrame& parsed, uint64_t completed_us) noexcept {
    transport::KwpFrameEnvelope frame{};
    frame.size = parsed.size;
    frame.counter = parsed.counter;
    frame.title = parsed.size > 2 ? engine_.frameData()[2] : 0;
    frame.sessionEpoch = session_epoch_;
    frame.firstByteUs = frame_first_byte_us_;
    frame.completedUs = completed_us;
    frame.groupHint = plan_.group();
    frame.transportGeneration = generation_;
    frame.ingressEpoch = active_ingress_epoch_;
    for (uint8_t i = 0; i < parsed.size; ++i) frame.bytes[i] = engine_.frameData()[i];
    if (frame_sink_ != nullptr) (void)frame_sink_->trySend(frame);
  }

  bool buildCommand(const PlanCommand& command) noexcept {
    uint8_t payload_size = 0;
    uint8_t title = 0;
    if (command.kind == PlanCommandKind::Ack) {
      title = 0x09;
    } else if (command.kind == PlanCommandKind::GroupZero) {
      title = 0x12;
    } else {
      title = 0x29;
      tx_frame_[3] = command.group;
      payload_size = 1;
    }
    const uint8_t block_length = static_cast<uint8_t>(payload_size + 3);
    tx_frame_[0] = block_length;
    tx_frame_[1] = tx_counter_++;
    tx_frame_[2] = title;
    tx_frame_[3 + payload_size] = 0x03;
    tx_frame_size_ = static_cast<uint8_t>(block_length + 1);
    tx_index_ = 0;
    tx_block_active_ = true;
    return startCurrentHostByte();
  }

  bool startCurrentHostByte() noexcept {
    if (!tx_block_active_ || tx_index_ >= tx_frame_size_) return false;
    const bool requires_ack = tx_index_ + 1 < tx_frame_size_;
    const digifant::kwp::KwpOpToken token{
        generation_, session_epoch_, turn_id_ + 1, next_operation_id_,
        static_cast<uint8_t>(digifant::kwp::ActionKind::SendByte)};
    updateTraceContext(last_event_timestamp_us_, token);
    if (!engine_.beginHostByte(tx_frame_[tx_index_], requires_ack)) {
      state_ = MeasurementSessionState::Fault;
      return false;
    }
    auto request = engine_.takeTxRequest();
    if (!request.pending || action_queued_ || action_issued_) {
      state_ = MeasurementSessionState::Fault;
      return false;
    }
    queued_action_ = digifant::kwp::KwpAction{
        digifant::kwp::ActionKind::SendByte, request.byte,
        token,
        0};
    ++turn_id_;
    ++next_operation_id_;
    action_queued_ = true;
    engine_.noteTxTokenAssigned(request.byte);
    return true;
  }

  bool advanceAfterTransportEvent() noexcept {
    if (action_queued_ || action_issued_) return true;
    if (!tx_block_active_) {
      auto request = engine_.takeTxRequest();
      if (!request.pending) return true;
      if (action_queued_) {
        state_ = MeasurementSessionState::Fault;
        return false;
      }
      queued_action_ = digifant::kwp::KwpAction{
          digifant::kwp::ActionKind::SendByte, request.byte,
          digifant::kwp::KwpOpToken{generation_, session_epoch_, ++turn_id_,
                                    next_operation_id_++,
                                    static_cast<uint8_t>(digifant::kwp::ActionKind::SendByte)},
          0};
      action_queued_ = true;
      updateTraceContext(last_event_timestamp_us_, queued_action_.token);
      engine_.noteTxTokenAssigned(request.byte);
      return true;
    }
    if (engine_.state() != digifant::kwp::ByteEngineState::Idle) return true;
    if (++tx_index_ >= tx_frame_size_) {
      tx_block_active_ = false;
      engine_.reset();
      return true;
    }
    return startCurrentHostByte();
  }

  digifant::kwp::KwpOpToken activeOperationToken() const noexcept {
    if (action_queued_ || action_issued_) return queued_action_.token;
    return {};
  }

  uint8_t currentBlockCounter() const noexcept {
    return tx_block_active_ && tx_frame_size_ > 1 ? tx_frame_[1] : tx_counter_;
  }

  void updateTraceContext(uint64_t timestamp_us,
                          const digifant::kwp::KwpOpToken& token) noexcept {
    engine_.setTraceContext(digifant::kwp::ByteEngineTraceContext{
        timestamp_us,
        generation_,
        session_epoch_,
        token.semanticTurnId,
        token.transportOpId,
        static_cast<uint8_t>(state_),
        static_cast<uint8_t>(plan_.stage()),
        plan_.group(),
        currentBlockCounter()});
  }

  MeasurementSessionState state_ = MeasurementSessionState::Idle;
  digifant::kwp::KwpByteEngine engine_{};
  MeasurementPlan plan_{};
  std::array<uint8_t, digifant::kwp::KwpByteEngine::kMaxFrame> tx_frame_{};
  digifant::kwp::KwpAction queued_action_{};
  uint8_t tx_frame_size_ = 0;
  uint8_t tx_index_ = 0;
  uint8_t tx_counter_ = 0;
  bool tx_block_active_ = false;
  bool action_queued_ = false;
  bool action_issued_ = false;
  bool reset_after_final_echo_ = false;
  uint32_t generation_ = 0;
  uint32_t session_epoch_ = 0;
  uint32_t stale_completions_ = 0;
  uint32_t stale_rx_items_ = 0;
  uint32_t ingress_epoch_ = 0;
  uint32_t active_ingress_epoch_ = 0;
  uint16_t parsed_frames_ = 0;
  uint16_t identification_frames_ = 0;
  uint16_t ack_frames_ = 0;
  uint16_t group_header_frames_ = 0;
  uint16_t group_body_frames_ = 0;
  uint16_t refused_frames_ = 0;
  uint16_t parser_rejected_ = 0;
  transport::ValidatedFrameQueue* frame_sink_ = nullptr;
  uint64_t frame_first_byte_us_ = 0;
  uint64_t last_event_timestamp_us_ = 0;
  uint64_t turn_id_ = 0;
  uint64_t next_operation_id_ = 1;
};

}  // namespace digifant::application
