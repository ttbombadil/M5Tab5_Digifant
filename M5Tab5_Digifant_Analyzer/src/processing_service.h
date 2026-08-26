#pragma once

#include "diagnostic_decoder.h"
#include "measurement_model.h"
#include "measurement_snapshot_mailbox.h"
#include "rx_ingress_ring.h"
#include "logger_channels.h"
#include "validated_frame_queue.h"

#include <cstdint>

namespace digifant::processing {

// Runtime-owned values are sampled by the task entry and passed by value. The
// service does not read transport/KWP globals and therefore owns only decoder,
// model, and snapshot publication.
struct RuntimeStatus {
  bool k409Connected = false;
  bool kwpActive = false;
  uint32_t transportGeneration = 0;
  uint32_t sessionEpoch = 0;
  uint32_t parserRejects = 0;
  uint32_t actionFailures = 0;
  uint8_t byteEngineFault = 0;
};

class ProcessingService {
 public:
  ProcessingService(transport::ValidatedFrameQueue& frames,
                    transport::RxIngressRing& rxIngress,
                    ui::SnapshotConsumerFanout& snapshots,
                    logging::LoggerSnapshotQueue& loggerSnapshots) noexcept
      : frames_(frames),
        rxIngress_(rxIngress),
        snapshots_(snapshots),
        loggerSnapshots_(loggerSnapshots) {}

  // Execute one bounded processing poll. The status provider is invoked only
  // after the frame drain, matching the former task ordering exactly.
  template <typename RuntimeStatusProvider>
  bool poll(RuntimeStatusProvider statusProvider) noexcept {
    transport::KwpFrameEnvelope record{};
    bool consumed = false;
    while (frames_.tryReceive(record)) {
      if (haveSequence_ && record.rxSequence != expectedSequence_) {
        decoder_.resetSession();
        measurementModel_.onSequenceGap();
      }
      expectedSequence_ = record.rxSequence + 1U;
      haveSequence_ = true;
      processRecord(record);
      consumed = true;
    }

    const RuntimeStatus status = statusProvider();
    bool statusChanged = false;
    if (status.k409Connected != lastK409Connected_ || status.kwpActive != lastKwpActive_ ||
        status.transportGeneration != lastStatusGeneration_ ||
        status.sessionEpoch != lastStatusSession_) {
      if (!status.kwpActive) decoder_.resetSession();
      measurementModel_.onProtocolStatus(status.k409Connected, status.kwpActive,
                                          status.transportGeneration, status.sessionEpoch);
      lastK409Connected_ = status.k409Connected;
      lastKwpActive_ = status.kwpActive;
      lastStatusGeneration_ = status.transportGeneration;
      lastStatusSession_ = status.sessionEpoch;
      statusChanged = true;
    }

    const uint32_t snapshotOverwrites = snapshots_.overwrites();
    const bool telemetryChanged = measurementModel_.onRuntimeTelemetry(
        frames_.nextSequence(), frames_.drops(), rxIngress_.dropped(),
        status.parserRejects, status.actionFailures, status.byteEngineFault,
        snapshotOverwrites);
    if (consumed || statusChanged || telemetryChanged)
      snapshots_.publish(measurementModel_.snapshot());
    return consumed;
  }

  bool poll(const RuntimeStatus& status) noexcept {
    return poll([&status]() noexcept { return status; });
  }

 private:
  void processRecord(const transport::KwpFrameEnvelope& record) noexcept {
    const auto result = decoder_.process(record);
    (void)measurementModel_.apply(record, result);
    (void)loggerSnapshots_.trySend(measurementModel_.snapshot());
  }

  transport::ValidatedFrameQueue& frames_;
  transport::RxIngressRing& rxIngress_;
  ui::SnapshotConsumerFanout& snapshots_;
  logging::LoggerSnapshotQueue& loggerSnapshots_;
  diagnostic::DiagnosticDecoder decoder_;
  application::MeasurementModel measurementModel_;
  uint32_t expectedSequence_ = 0;
  bool haveSequence_ = false;
  bool lastK409Connected_ = false;
  bool lastKwpActive_ = false;
  uint32_t lastStatusGeneration_ = 0xFFFFFFFFU;
  uint32_t lastStatusSession_ = 0xFFFFFFFFU;
};

}  // namespace digifant::processing
