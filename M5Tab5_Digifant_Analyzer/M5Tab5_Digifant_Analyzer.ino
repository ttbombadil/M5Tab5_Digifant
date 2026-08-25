#include <Arduino.h>
#include <M5Unified.h>
#include <esp_system.h>
#include "src/esp_usb_host_fork/EspUsbHost.h"
#include <atomic>
#include "src/k409_device_filter.h"
#include "src/rx_ingress_ring.h"
#include "src/critical_transport_event_ring.h"
#include "src/kwp1281_core.h"
#include "src/kwp_measurement_session.h"
#include "src/kwp_reconnect_policy.h"
#include "src/processing_service.h"
#include "src/serial_consumer.h"
#include "src/measurement_snapshot.h"
#include "src/ui_state.h"
#include "src/display_ui.h"
#include "src/sprotz_logger.h"
#include "src/sprotz_logger_target.h"
#include "src/imu_sampler.h"
#include "src/imu_target_adapter.h"
#include "src/imu_sample_ring.h"
#include "src/logger_time_merge.h"

namespace {
EspUsbHost usb_host;
digifant::transport::RxIngressRing rx_ingress;
digifant::transport::CriticalTransportEventRing critical_events;
digifant::transport::ValidatedFrameQueue frame_queue;
digifant::ui::SnapshotConsumerFanout snapshot_fanout;
digifant::logging::LoggerSnapshotQueue logger_snapshot_queue;
digifant::logging::LoggerCommandQueue logger_command_queue;
digifant::processing::ProcessingService processing_service(
    frame_queue, rx_ingress, snapshot_fanout, logger_snapshot_queue);
digifant::logging::LoggerStatusFanout logger_status_fanout;
digifant::imu::ImuSampleRing imu_sample_ring;
digifant::logging::SprotzLoggerService sprotz_logger_service(
    logger_snapshot_queue, logger_command_queue, imu_sample_ring, logger_status_fanout);
class ImuDiagnosticsSink final : public digifant::imu::IImuSink {
 public:
  ImuDiagnosticsSink(digifant::imu::ImuSampleRing& ring,
                     digifant::imu::ImuDiagnosticsMailbox& diagnostics)
      : ring_(ring), diagnostics_(diagnostics) {}
  bool tryPublish(const digifant::imu::ImuSample& sample) noexcept override {
    if (!ring_.tryPush(sample)) return false;
    diagnostics_.publish(sample);
    return true;
  }
  digifant::imu::ImuSampleRing& ring_;
  digifant::imu::ImuDiagnosticsMailbox& diagnostics_;
};
digifant::imu::M5Tab5ImuSource imu_source;
digifant::imu::ImuDiagnosticsMailbox imu_diagnostics_mailbox;
ImuDiagnosticsSink imu_diagnostics_sink(imu_sample_ring, imu_diagnostics_mailbox);
digifant::imu::ImuSampler imu_sampler(imu_source, imu_diagnostics_sink);
TaskHandle_t processing_task = nullptr;
TaskHandle_t serial_snapshot_task = nullptr;
TaskHandle_t display_snapshot_task = nullptr;
TaskHandle_t bluetooth_snapshot_task = nullptr;
TaskHandle_t web_snapshot_task = nullptr;
TaskHandle_t sprotz_logger_task = nullptr;
TaskHandle_t imu_sampler_task = nullptr;
digifant::ui::DisplayUi display_ui;
std::atomic<uint8_t> device_state{0};
std::atomic<uint8_t> device_address{0};
std::atomic<uint8_t> serial_tab_request{255};
std::atomic<uint32_t> transport_generation{0};
std::atomic<uint64_t> transport_sequence{1};
std::atomic<uint32_t> action_failures{0};
std::atomic<uint32_t> parser_rejects{0};
std::atomic<uint8_t> byte_engine_fault{0};
std::atomic<bool> transport_reuse_blocked{false};
std::atomic<bool> kwp_session_active{false};
std::atomic<uint32_t> active_session_epoch{0};
digifant::application::KwpReconnectPolicy reconnect_policy;
uint32_t next_session_epoch = 1;
uint64_t next_transport_operation_id = 1;
constexpr uint8_t kConnected = 1;

#ifndef V2_015_TARGET_STRESS
#define V2_015_TARGET_STRESS 0
#endif

uint64_t serial_now_us(void*) noexcept { return static_cast<uint64_t>(micros()); }

uint32_t serial_logger_stack_free_words(void* context) noexcept {
  const auto task = *static_cast<TaskHandle_t*>(context);
  return task != nullptr ? static_cast<uint32_t>(uxTaskGetStackHighWaterMark(task)) : 0U;
}

digifant::serial::SerialConsumer<decltype(Serial)> serial_consumer(
    logger_command_queue, logger_status_fanout.serial(), snapshot_fanout.serial(),
    imu_diagnostics_mailbox, serial_tab_request, Serial);

void processing_task_entry(void*) {
  for (;;) {
    const bool consumed = processing_service.poll([]() noexcept {
      return digifant::processing::RuntimeStatus{
          device_state.load(std::memory_order_acquire) == kConnected,
          kwp_session_active.load(std::memory_order_acquire),
          transport_generation.load(std::memory_order_acquire),
          active_session_epoch.load(std::memory_order_acquire),
          parser_rejects.load(std::memory_order_acquire),
          action_failures.load(std::memory_order_acquire),
          byte_engine_fault.load(std::memory_order_acquire)};
    });
    vTaskDelay(pdMS_TO_TICKS(consumed ? 1 : 10));
  }
}

void serial_snapshot_task_entry(void*) {
  for (;;) {
    serial_consumer.poll(serial_now_us, nullptr,
                         sprotz_logger_task != nullptr ? serial_logger_stack_free_words : nullptr,
                         &sprotz_logger_task);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void display_snapshot_task_entry(void*) {
  uint32_t last_stall_ms = 0;
  for (;;) {
    const uint8_t requested_tab = serial_tab_request.exchange(255, std::memory_order_acq_rel);
    if (requested_tab < 4U) display_ui.setTabFromSerial(requested_tab);
    digifant::ui::MeasurementSnapshot next{};
    if (snapshot_fanout.display().receive(next)) display_ui.consume(next);
    digifant::logging::LoggerStatus logger_status{};
    if (logger_status_fanout.display().receive(logger_status)) display_ui.consumeLoggerStatus(logger_status);
    display_ui.update();
    const uint32_t now = millis();
    if (V2_015_TARGET_STRESS && now - last_stall_ms >= 4000U) {
      last_stall_ms = now;
      vTaskDelay(pdMS_TO_TICKS(500));
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void sprotz_logger_task_entry(void*) {
  sprotz_logger_service.begin();
  for (;;) {
    sprotz_logger_service.poll(static_cast<uint64_t>(esp_timer_get_time()));
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void imu_sampler_task_entry(void*) {
  for (;;) {
    const uint64_t now = static_cast<uint64_t>(esp_timer_get_time());
    (void)imu_sampler.poll(now);
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

struct DummySnapshotConsumer {
  digifant::ui::LatestSnapshotMailbox* mailbox = nullptr;
  uint32_t delayMs = 1000;
};

DummySnapshotConsumer bluetooth_consumer{&snapshot_fanout.bluetooth(), 1000};
DummySnapshotConsumer web_consumer{&snapshot_fanout.web(), 3000};

void dummy_snapshot_task_entry(void* argument) {
  auto* consumer = static_cast<DummySnapshotConsumer*>(argument);
  for (;;) {
    digifant::ui::MeasurementSnapshot snapshot{};
    (void)consumer->mailbox->receive(snapshot);
    vTaskDelay(pdMS_TO_TICKS(consumer->delayMs));
  }
}

void on_device_connected(const EspUsbHostDeviceInfo& info) {
  if (!digifant::k409::matches(info.vid, info.pid)) return;
  device_address.store(info.address, std::memory_order_release);
  transport_generation.fetch_add(1, std::memory_order_acq_rel);
  transport_reuse_blocked.store(false, std::memory_order_release);
  device_state.store(kConnected, std::memory_order_release);
}

void on_device_disconnected(const EspUsbHostDeviceInfo& info) {
  if (!digifant::k409::matches(info.vid, info.pid)) return;
  const uint32_t generation = transport_generation.load(std::memory_order_acquire);
  const uint32_t at = micros();
  device_state.store(0, std::memory_order_release);
  const uint64_t sequence = transport_sequence.fetch_add(1, std::memory_order_relaxed);
  (void)critical_events.tryPush({digifant::transport::CriticalTransportEventKind::Disconnect,
                                 0, info.address, generation, 0, 0, sequence, at});
}

void on_serial_data(const EspUsbHostSerialData& data) {
  if (!data.data || data.address != device_address.load(std::memory_order_acquire)) return;
  const uint64_t sequence = transport_sequence.fetch_add(data.length, std::memory_order_relaxed);
  (void)rx_ingress.publishBatch(data.data, data.length, micros(),
                                 transport_generation.load(std::memory_order_acquire), sequence);
}

void on_operation_completion(const EspUsbHostOperationCompletion& completion) {
  const uint64_t sequence = transport_sequence.fetch_add(1, std::memory_order_relaxed);
  (void)critical_events.tryPush({digifant::transport::CriticalTransportEventKind::Completion,
                                 static_cast<uint8_t>(completion.status), completion.address,
                                 completion.token.transportGeneration, completion.token.operationId,
                                 completion.token.operationKind, sequence, completion.completedAtUs});
}

EspUsbHostOperationToken to_transport_token(const digifant::kwp::KwpOpToken& token) {
  return {token.transportGeneration, token.transportOpId, token.operationKind};
}

bool submit_action(const digifant::kwp::KwpAction& action, uint8_t address) {
  const auto token = to_transport_token(action.token);
  if (action.kind == digifant::kwp::ActionKind::SetBaud) {
    EspUsbHostSerialConfig config{};
    config.baud = action.value;
    config.dataBits = 8;
    config.parity = ESP_USB_HOST_SERIAL_PARITY_NONE;
    config.stopBits = ESP_USB_HOST_SERIAL_STOP_BITS_1;
    return usb_host.setSerialConfigTokenized(config, token, address);
  }
  if (action.kind == digifant::kwp::ActionKind::SetBreak)
    return usb_host.submitVendorSerialControlTokenized(0x40, 0x04,
      action.value ? 0x4000 : 0x0008, 0x0001, token, address);
  const uint8_t byte = static_cast<uint8_t>(action.value);
  if (!usb_host.serialWriteQueueReady(address) && !usb_host.serialWriteQueueBegin(1, 1, address)) return false;
  return usb_host.serialWriteTokenized(&byte, 1, token, address);
}

bool wait_completion(const digifant::kwp::KwpOpToken& token, uint32_t timeout_ms,
                     digifant::kwp::CompletionStatus& status) {
  const uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    digifant::transport::CriticalTransportEvent event{};
    while (critical_events.tryPop(event)) {
      if (event.kind == digifant::transport::CriticalTransportEventKind::Disconnect &&
          event.generation == token.transportGeneration) {
        status = digifant::kwp::CompletionStatus::Canceled;
        return true;
      }
      if (event.kind != digifant::transport::CriticalTransportEventKind::Completion ||
          event.generation != token.transportGeneration || event.operationId != token.transportOpId ||
          event.operationKind != token.operationKind) continue;
      status = static_cast<digifant::kwp::CompletionStatus>(event.status);
      return true;
    }
    if (critical_events.overflowSticky()) {
      status = digifant::kwp::CompletionStatus::OutcomeUnknown;
      transport_reuse_blocked.store(true, std::memory_order_release);
      return false;
    }
    delay(1);
  }
  status = digifant::kwp::CompletionStatus::OutcomeUnknown;
  transport_reuse_blocked.store(true, std::memory_order_release);
  return false;
}

bool execute_core_action(const digifant::kwp::KwpAction& action, uint8_t address,
                         digifant::kwp::KwpProtocolCore& core) {
  if (!submit_action(action, address)) {
    core.completion(action.token, digifant::kwp::CompletionStatus::Failed);
    return false;
  }
  digifant::kwp::CompletionStatus status{};
  (void)wait_completion(action.token, 1000, status);
  core.completion(action.token, status);
  return status == digifant::kwp::CompletionStatus::Completed;
}

bool run_handshake(uint32_t generation, uint8_t address, uint32_t session_epoch,
                   uint64_t& next_operation_id) {
  const uint8_t requests[] = {0x09, 0x01, 0x01};
  const uint16_t values[] = {1, 0x0100, 0x0200};
  for (uint8_t i = 0; i < 3; ++i) {
    const EspUsbHostOperationToken preinit{generation, next_operation_id++, 2};
    if (!usb_host.submitVendorSerialControlTokenized(0x40, requests[i], values[i], 1,
                                                      preinit, address)) return false;
    digifant::kwp::CompletionStatus preinit_status{};
    const digifant::kwp::KwpOpToken token{generation, session_epoch, 0, preinit.operationId,
                                          preinit.operationKind};
    (void)wait_completion(token, 1000, preinit_status);
    if (preinit_status != digifant::kwp::CompletionStatus::Completed) return false;
  }
  const uint32_t start = millis();
  digifant::kwp::KwpProtocolCore core;
  core.connectedWithIds(generation, session_epoch, next_operation_id, micros());
  while (millis() - start < 5000 && core.state() != digifant::kwp::CoreState::Active &&
         core.state() != digifant::kwp::CoreState::Fault) {
    if (device_state.load(std::memory_order_acquire) != kConnected ||
        transport_generation.load(std::memory_order_acquire) != generation) {
      next_operation_id = core.nextOperationId();
      return false;
    }
    digifant::transport::RxIngressItem item{};
    while (rx_ingress.tryPop(item)) if (item.transportGeneration == generation) core.rxByte(item.byte, item.batchTimestampUs);
    core.advance(micros());
    digifant::kwp::KwpAction action{};
    while (core.popAction(action)) {
      if (!execute_core_action(action, address, core)) {
        next_operation_id = core.nextOperationId();
        return false;
      }
    }
    delay(1);
  }
  next_operation_id = core.nextOperationId();
  return core.state() == digifant::kwp::CoreState::Active;
}

bool run_measurement(uint32_t generation, uint8_t address, uint32_t session_epoch,
                     uint64_t& next_operation_id) {
  digifant::application::KwpMeasurementSession session;
  session.setValidatedFrameSink(frame_queue);
  session.start(generation, session_epoch, next_operation_id);
  action_failures.store(0, std::memory_order_relaxed);
  parser_rejects.store(0, std::memory_order_relaxed);
  byte_engine_fault.store(0, std::memory_order_relaxed);
  const uint32_t start = millis();
  uint32_t last_progress_ms = start;
  uint32_t last_telemetry_ms = start;
  uint32_t bytes = 0;
  while (session.state() == digifant::application::MeasurementSessionState::Running) {
    if (device_state.load(std::memory_order_acquire) != kConnected ||
        transport_generation.load(std::memory_order_acquire) != generation) {
      session.disconnect();
      break;
    }
    digifant::transport::RxIngressItem item{};
    while (rx_ingress.tryPop(item)) {
      if (item.transportGeneration != generation) continue;
      ++bytes;
      last_progress_ms = millis();
      if (!session.onRxItem(item)) break;
      digifant::kwp::KwpAction action{};
      while (session.popAction(action)) {
        if (!submit_action(action, address)) {
          ++action_failures;
          session.onCompletion(action.token, digifant::kwp::CompletionStatus::Failed, micros());
          break;
        }
        digifant::kwp::CompletionStatus status{};
        (void)wait_completion(action.token, 1000, status);
        if (status != digifant::kwp::CompletionStatus::Completed) ++action_failures;
        if (!session.onCompletion(action.token, status, micros())) break;
      }
    }
    const uint32_t now = millis();
    if (now - last_telemetry_ms >= 250U) {
      last_telemetry_ms = now;
      parser_rejects.store(session.parserRejected(), std::memory_order_release);
      byte_engine_fault.store(session.byteEngineFault(), std::memory_order_release);
    }
    if (digifant::application::KwpReconnectPolicy::sessionStalled(millis(), last_progress_ms)) {
      session.disconnect();
      break;
    }
    delay(1);
  }
  next_operation_id = session.nextOperationId();
  parser_rejects.store(session.parserRejected(), std::memory_order_release);
  byte_engine_fault.store(session.byteEngineFault(), std::memory_order_release);
  return session.state() == digifant::application::MeasurementSessionState::Running && bytes != 0;
}

void run_session() {
  const bool connected = device_state.load(std::memory_order_acquire) == kConnected;
  const uint32_t generation = transport_generation.load(std::memory_order_acquire);
  if (transport_reuse_blocked.load(std::memory_order_acquire)) return;
  if (!reconnect_policy.shouldAttempt(connected, generation, millis())) return;
  if (rx_ingress.state() == digifant::transport::RxIngressRing::State::Poisoned &&
      !rx_ingress.resetAfterQuiescence()) return;
  digifant::transport::RxIngressItem stale_item{};
  while (rx_ingress.tryPop(stale_item)) {}
  const uint8_t address = device_address.load(std::memory_order_acquire);
  const uint32_t session_epoch = next_session_epoch++;
  if (next_session_epoch == 0) next_session_epoch = 1;
  const bool ok = run_handshake(generation, address, session_epoch, next_transport_operation_id);
  if (ok) {
    active_session_epoch.store(session_epoch, std::memory_order_release);
    kwp_session_active.store(true, std::memory_order_release);
    const bool running = run_measurement(generation, address, session_epoch,
                                         next_transport_operation_id);
    (void)running;
  }
  kwp_session_active.store(false, std::memory_order_release);
  reconnect_policy.attemptFinished(millis());
}
}  // namespace

namespace {
constexpr uint8_t kDisplayInitAttempts = 5;
constexpr uint32_t kDisplayInitRetryMs = 250;
RTC_DATA_ATTR uint8_t display_boot_recovery_attempted = 0;

bool ensure_tab5_display_ready() {
  if (M5.Display.width() > 0 && M5.Display.height() > 0) return true;

  // M5GFX intentionally exposes no public way to repeat its failed one-shot
  // autodetection after M5.begin(). A single early restart is therefore the
  // bounded equivalent of the proven manual-reset recovery: the first boot
  // establishes Tab5 power/reset state, the second performs a clean probe.
  if (display_boot_recovery_attempted == 0) {
    display_boot_recovery_attempted = 1;
    Serial.flush();
    delay(100);
    ESP.restart();
  }

  // A genuine persistent panel fault must not cause a reboot loop.
  for (uint8_t attempt = 1; attempt <= kDisplayInitAttempts; ++attempt) {
    delay(kDisplayInitRetryMs);
    const bool initialized = M5.Display.init();
    if (!initialized || M5.Display.width() <= 0 || M5.Display.height() <= 0) continue;
    if (M5.getDisplayCount() == 0) M5.addDisplay(M5.Display);
    return true;
  }
  return false;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.internal_imu = true;
  M5.begin(cfg);
  if (!ensure_tab5_display_ready()) action_failures.fetch_add(1, std::memory_order_relaxed);
  else display_boot_recovery_attempted = 0;
  display_ui.bindLogger(logger_command_queue);
  display_ui.begin();
  if (xTaskCreate(processing_task_entry, "processing", 6144, nullptr, 3, &processing_task) != pdPASS)
    action_failures.fetch_add(1, std::memory_order_relaxed);
  if (xTaskCreate(serial_snapshot_task_entry, "serial_snapshot_task", 4096, nullptr, 1,
                  &serial_snapshot_task) != pdPASS)
    action_failures.fetch_add(1, std::memory_order_relaxed);
  if (xTaskCreate(display_snapshot_task_entry, "display_snapshot_task", 12288, nullptr, 2,
                  &display_snapshot_task) != pdPASS)
    action_failures.fetch_add(1, std::memory_order_relaxed);
  if (xTaskCreate(dummy_snapshot_task_entry, "bluetooth_snapshot_dummy", 3072,
                  &bluetooth_consumer, 1, &bluetooth_snapshot_task) != pdPASS)
    action_failures.fetch_add(1, std::memory_order_relaxed);
  if (xTaskCreate(dummy_snapshot_task_entry, "web_snapshot_dummy", 3072,
                  &web_consumer, 1, &web_snapshot_task) != pdPASS)
    action_failures.fetch_add(1, std::memory_order_relaxed);
  if (xTaskCreate(sprotz_logger_task_entry, "sprotz_logger", 16384, nullptr, 1,
                  &sprotz_logger_task) != pdPASS)
    action_failures.fetch_add(1, std::memory_order_relaxed);
  if (xTaskCreate(imu_sampler_task_entry, "imu_sampler", 3072, nullptr, 1,
                  &imu_sampler_task) != pdPASS)
    action_failures.fetch_add(1, std::memory_order_relaxed);
  usb_host.onDeviceConnected(on_device_connected);
  usb_host.onDeviceDisconnected(on_device_disconnected);
  usb_host.onSerialData(on_serial_data);
  usb_host.onOperationCompletion(on_operation_completion);
  if (!usb_host.begin()) action_failures.fetch_add(1, std::memory_order_relaxed);
}

void loop() { run_session(); delay(20); }
