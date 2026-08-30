#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ui_state.h"

// Renderer plus passive audio preparation. Microphone runtime, speaker and SD
// remain disabled so this stage cannot start I2S or an audio recording.

namespace {

using audio_probe::TransitionResult;
using audio_probe::EffectRequest;
using audio_probe::UiEvent;
using audio_probe::UiModel;
using audio_probe::UiState;

constexpr int16_t kFooterHeight = 54;
constexpr uint32_t kHeartbeatMs = 5000;
constexpr uint32_t kTouchPollMs = 20;
constexpr uint32_t kTouchReleaseMs = 30;
constexpr uint32_t kTouchHealthMs = 2000;
constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kMinCaptureSeconds = 20;
constexpr uint32_t kMaxCaptureSeconds = 30;
constexpr uint32_t kChunkFrames = kSampleRate / 4;  // 250 ms
constexpr uint32_t kMaxFrames = kSampleRate * kMaxCaptureSeconds;
constexpr uint32_t kMinFrames = kSampleRate * kMinCaptureSeconds;
constexpr int16_t kNearFullScale = 32752;
constexpr size_t kPcmSamples = static_cast<size_t>(kMaxFrames) * 2U;
constexpr size_t kChunkSamples = static_cast<size_t>(kChunkFrames) * 2U;
static_assert(kPcmSamples * sizeof(int16_t) == 1920000,
              "30 s stereo PCM reservation changed");
static_assert(kChunkSamples * sizeof(int16_t) == 16000,
              "250 ms stereo chunk reservation changed");
constexpr uint16_t kBackground = TFT_BLACK;
constexpr uint16_t kInactive = 0x2104;
constexpr uint16_t kDetail = 0x03EF;
constexpr uint16_t kCapturing = 0x0320;
constexpr uint16_t kConfirm = 0xB260;
constexpr uint16_t kResult = 0x4010;

const char* const kTestNames[audio_probe::kTestCount] = {
    "1/6 MOTOR AUS", "2/6 LEERLAUF", "3/6 1000 rpm",
    "4/6 1500 rpm", "5/6 2000 rpm", "6/6 2500 rpm"};

UiModel model;
bool touchWasDown = false;
uint32_t physicalTouches = 0;
uint32_t serialEvents = 0;
uint32_t acceptedEvents = 0;
uint32_t rejectedEvents = 0;
uint32_t renders = 0;
uint32_t lastRenderUs = 0;
uint32_t maxRenderUs = 0;
uint32_t lastHeartbeat = 0;
uint32_t lastTouchPoll = 0;
uint32_t touchReads = 0;
uint32_t touchIdleSkips = 0;
uint32_t touchIntTransitions = 0;
uint32_t touchIntHighSince = 0;
uint32_t lastTouchHealthCheck = 0;
uint32_t touchHealthChecks = 0;
uint32_t touchHealthFailures = 0;
uint32_t touchRecoveries = 0;
uint8_t lastTouchFirmware = 0;
int8_t lastTouchIntLevel = -1;
char command[32] = {};
size_t commandLength = 0;

struct AudioPreparation {
  int16_t* pcm = nullptr;
  int16_t* chunk = nullptr;
  bool configured = false;
  uint32_t startAttempts = 0;
  uint32_t startSuccesses = 0;
  uint32_t stopRequests = 0;
  uint32_t blockRequests = 0;
  uint32_t blockCompletions = 0;
  uint32_t blockFailures = 0;
  uint32_t abortedBlocks = 0;
  uint32_t completedCaptures = 0;
  uint32_t currentFrames = 0;
  uint32_t currentBlocks = 0;
  uint64_t captureAudioUs = 0;
  uint32_t blockStartedUs = 0;
  uint32_t lastDisplayedSecond = UINT32_MAX;
  bool blockPending = false;

  bool buffersReady() const { return pcm != nullptr && chunk != nullptr; }
};

AudioPreparation audio;

struct ChannelStats {
  int16_t minimum = INT16_MAX;
  int16_t maximum = INT16_MIN;
  int64_t sum = 0;
  uint64_t squares = 0;
  uint32_t nearFullScale = 0;
  uint32_t clippingEvents = 0;
  bool previousClipped = false;
};

struct CaptureResult {
  bool available = false;
  bool durationValid = false;
  bool levelWarning = false;
  uint32_t frames = 0;
  uint64_t audioUs = 0;
  ChannelStats channels[2];
};

ChannelStats captureChannels[2];
CaptureResult captureResults[audio_probe::kTestCount];

void collectSample(ChannelStats& stats, int16_t value) {
  if (value < stats.minimum) stats.minimum = value;
  if (value > stats.maximum) stats.maximum = value;
  stats.sum += value;
  const int64_t wide = value;
  stats.squares += static_cast<uint64_t>(wide * wide);
  const bool clipped = value <= -kNearFullScale || value >= kNearFullScale;
  if (clipped) {
    ++stats.nearFullScale;
    if (!stats.previousClipped) ++stats.clippingEvents;
  }
  stats.previousClipped = clipped;
}

bool prepareAudioWithoutStartingMicrophone() {
  auto mic = M5.Mic.config();
  mic.sample_rate = kSampleRate;
  mic.input_channel = m5::input_channel_t::input_stereo;
  mic.over_sampling = 1;
  mic.magnification = 2;
  mic.noise_filter_level = 0;
  M5.Mic.config(mic);
  audio.configured = true;

  audio.pcm = static_cast<int16_t*>(heap_caps_malloc(
      kPcmSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  audio.chunk = static_cast<int16_t*>(heap_caps_malloc(
      kChunkSamples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

  Serial.printf(
      "AUDIO_PREP configured=%s buffers=%s rate=%lu channels=2 pcm_bytes=%u "
      "chunk_bytes=%u psram_free=%u\n",
      audio.configured ? "yes" : "no", audio.buffersReady() ? "ready" : "failed",
      static_cast<unsigned long>(kSampleRate),
      static_cast<unsigned>(kPcmSamples * sizeof(int16_t)),
      static_cast<unsigned>(kChunkSamples * sizeof(int16_t)),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  return audio.configured && audio.buffersReady();
}

const char* effectName(EffectRequest effect) {
  switch (effect) {
    case EffectRequest::StartMicrophone: return "MIC_START";
    case EffectRequest::StopMicrophone: return "MIC_STOP";
    case EffectRequest::None: return "NONE";
  }
  return "UNKNOWN";
}

void runEffect(EffectRequest effect, bool resumeCapture = false) {
  if (effect == EffectRequest::None) return;

  const uint32_t startedUs = micros();
  Serial.printf(
      "EFFECT phase=before request=%s mic_running=%s reset_reason=%d "
      "int23=%d heap=%u psram_free=%u\n",
      effectName(effect), M5.Mic.isRunning() ? "yes" : "no",
      static_cast<int>(esp_reset_reason()), digitalRead(23),
      static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

  bool success = true;
  if (effect == EffectRequest::StartMicrophone) {
    ++audio.startAttempts;
    success = M5.Mic.begin();
    if (success && !resumeCapture) {
      ++audio.startSuccesses;
      audio.currentFrames = 0;
      audio.currentBlocks = 0;
      audio.captureAudioUs = 0;
      audio.lastDisplayedSecond = UINT32_MAX;
      audio.blockPending = false;
      captureChannels[0] = ChannelStats{};
      captureChannels[1] = ChannelStats{};
    } else if (success) {
      ++audio.startSuccesses;
    }
  } else {
    ++audio.stopRequests;
    if (audio.blockPending) ++audio.abortedBlocks;
    M5.Mic.end();
    audio.blockPending = false;
    success = !M5.Mic.isRunning();
  }

  Serial.printf(
      "EFFECT phase=after request=%s success=%s mic_running=%s duration_us=%lu "
      "reset_reason=%d int23=%d heap=%u psram_free=%u\n",
      effectName(effect), success ? "yes" : "no",
      M5.Mic.isRunning() ? "yes" : "no",
      static_cast<unsigned long>(micros() - startedUs),
      static_cast<int>(esp_reset_reason()), digitalRead(23),
      static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

int16_t contentHeight() { return M5.Display.height() - kFooterHeight; }
int16_t rowHeight() { return contentHeight() / audio_probe::kTestCount; }

const char* eventName(UiEvent event) {
  switch (event) {
    case UiEvent::SelectTest: return "SELECT_TEST";
    case UiEvent::Activate: return "ACTIVATE";
    case UiEvent::Stop: return "STOP";
    case UiEvent::Resume: return "RESUME";
    case UiEvent::ConfirmStop: return "CONFIRM_STOP";
    case UiEvent::CompleteCapture: return "COMPLETE_CAPTURE";
    case UiEvent::Repeat: return "REPEAT";
    case UiEvent::Back: return "BACK";
    case UiEvent::None: return "NONE";
  }
  return "UNKNOWN";
}

uint16_t activeColor() {
  switch (model.state) {
    case UiState::Detail: return kDetail;
    case UiState::Capturing: return kCapturing;
    case UiState::StopConfirm: return kConfirm;
    case UiState::Result: return kResult;
    case UiState::List: return kInactive;
  }
  return kInactive;
}

void drawCentered(const char* text, int16_t left, int16_t top, int16_t width,
                  int16_t height, uint8_t size = 2) {
  M5.Display.setTextSize(size);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(text, left + width / 2, top + height / 2);
  M5.Display.setTextDatum(top_left);
}

void drawRow(uint8_t row) {
  const int16_t width = M5.Display.width();
  const int16_t height = rowHeight();
  const int16_t top = row * height;
  const bool active = model.state != UiState::List && row == model.selectedTest;
  const uint16_t background = active ? activeColor() : kInactive;

  M5.Display.fillRect(0, top, width, height - 2, background);
  M5.Display.setTextColor(TFT_WHITE, background);

  if (!active) {
    drawCentered(kTestNames[row], 0, top, width, height - 2, 3);
    return;
  }

  const int16_t zoneWidth = width / 3;
  const int16_t titleHeight = height / 3;
  const int16_t actionTop = top + titleHeight;
  const int16_t actionHeight = height - titleHeight - 2;
  char activeTitle[128];
  const char* title = kTestNames[row];
  if (model.state == UiState::Capturing) {
    std::snprintf(activeTitle, sizeof(activeTitle), "%s | %lu / %lu s",
                  kTestNames[row],
                  static_cast<unsigned long>(audio.currentFrames / kSampleRate),
                  static_cast<unsigned long>(kMaxCaptureSeconds));
    title = activeTitle;
  } else if (model.state == UiState::Result && captureResults[row].available) {
    const CaptureResult& result = captureResults[row];
    const double rms0 = result.frames
        ? std::sqrt(static_cast<double>(result.channels[0].squares) / result.frames)
        : 0.0;
    const double rms1 = result.frames
        ? std::sqrt(static_cast<double>(result.channels[1].squares) / result.frames)
        : 0.0;
    std::snprintf(activeTitle, sizeof(activeTitle), "%s | RMS %.0f / %.0f | %s",
                  kTestNames[row], rms0, rms1,
                  result.levelWarning ? "PEGELRESERVE" :
                  result.durationValid ? "OK" : "ZU KURZ");
    title = activeTitle;
  }
  drawCentered(title, 0, top, width, titleHeight, 2);
  M5.Display.drawFastHLine(0, actionTop, width, TFT_LIGHTGREY);
  M5.Display.drawFastVLine(zoneWidth, actionTop, actionHeight, TFT_LIGHTGREY);
  M5.Display.drawFastVLine(zoneWidth * 2, actionTop, actionHeight, TFT_LIGHTGREY);

  const char* left = "";
  const char* middle = "";
  const char* right = "";
  switch (model.state) {
    case UiState::Detail:
      left = "LISTE"; middle = "START MIC"; right = "START MIC"; break;
    case UiState::Capturing:
      left = middle = right = "STOP RECORD"; break;
    case UiState::StopConfirm:
      left = "WEITER"; middle = right = "STOP OK"; break;
    case UiState::Result:
      left = "LISTE"; middle = "WIEDERHOLEN"; right = "LISTE"; break;
    case UiState::List:
      break;
  }
  drawCentered(left, 0, actionTop, zoneWidth, actionHeight);
  drawCentered(middle, zoneWidth, actionTop, zoneWidth, actionHeight);
  drawCentered(right, zoneWidth * 2, actionTop,
               width - zoneWidth * 2, actionHeight);
}

void drawFooter() {
  const int16_t top = contentHeight();
  const int16_t width = M5.Display.width();
  char line[160];
  std::snprintf(line, sizeof(line),
                "%s | Touch %lu | Serial %lu | OK %lu | Reject %lu",
                audio_probe::stateName(model.state),
                static_cast<unsigned long>(physicalTouches),
                static_cast<unsigned long>(serialEvents),
                static_cast<unsigned long>(acceptedEvents),
                static_cast<unsigned long>(rejectedEvents));
  M5.Display.fillRect(0, top, width, kFooterHeight, TFT_DARKGREY);
  M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREY);
  drawCentered(line, 0, top, width, kFooterHeight, 2);
}

void finishRender(uint32_t startedUs, const char* scope) {
  M5.Display.display();
  lastRenderUs = micros() - startedUs;
  if (lastRenderUs > maxRenderUs) maxRenderUs = lastRenderUs;
  ++renders;
  Serial.printf("RENDER scope=%s number=%lu duration_us=%lu max_us=%lu\n",
                scope, static_cast<unsigned long>(renders),
                static_cast<unsigned long>(lastRenderUs),
                static_cast<unsigned long>(maxRenderUs));
}

void drawAll() {
  const uint32_t startedUs = micros();
  M5.Display.fillScreen(kBackground);
  for (uint8_t row = 0; row < audio_probe::kTestCount; ++row) drawRow(row);
  drawFooter();
  finishRender(startedUs, "full_boot");
}

void drawTransition(const UiModel& before) {
  const uint32_t startedUs = micros();
  if (before.selectedTest != model.selectedTest) drawRow(before.selectedTest);
  drawRow(model.selectedTest);
  drawFooter();
  finishRender(startedUs, before.selectedTest == model.selectedTest
                              ? "active_row" : "two_rows");
}

void drawFooterUpdate(const char* scope) {
  const uint32_t startedUs = micros();
  drawFooter();
  finishRender(startedUs, scope);
}

void printStatus(const char* reason) {
  Serial.printf(
      "STATUS reason=%s state=%s selected=%u touch=%lu serial=%lu accepted=%lu "
      "rejected=%lu renders=%lu render_last_us=%lu render_max_us=%lu reads=%lu "
      "idle_skips=%lu gate=%s int23=%d int_edges=%lu cached_count=%u "
      "health=%lu health_fail=%lu recoveries=%lu fw=0x%02X uptime_ms=%lu heap=%lu\n",
      reason, audio_probe::stateName(model.state), model.selectedTest + 1,
      static_cast<unsigned long>(physicalTouches),
      static_cast<unsigned long>(serialEvents),
      static_cast<unsigned long>(acceptedEvents),
      static_cast<unsigned long>(rejectedEvents),
      static_cast<unsigned long>(renders),
      static_cast<unsigned long>(lastRenderUs),
      static_cast<unsigned long>(maxRenderUs),
      static_cast<unsigned long>(touchReads),
      static_cast<unsigned long>(touchIdleSkips),
      touchWasDown ? "down" : "up",
      digitalRead(23), static_cast<unsigned long>(touchIntTransitions),
      static_cast<unsigned>(M5.Touch.getCount()),
      static_cast<unsigned long>(touchHealthChecks),
      static_cast<unsigned long>(touchHealthFailures),
      static_cast<unsigned long>(touchRecoveries), lastTouchFirmware,
      static_cast<unsigned long>(millis()),
      static_cast<unsigned long>(ESP.getFreeHeap()));
  Serial.printf(
      "AUDIO_STATUS configured=%s buffers=%s mic_running=%s starts=%lu/%lu "
      "stops=%lu blocks=%lu/%lu block_fail=%lu aborted=%lu captures=%lu "
      "current_frames=%lu psram_free=%u\n",
      audio.configured ? "yes" : "no", audio.buffersReady() ? "ready" : "failed",
      M5.Mic.isRunning() ? "yes" : "no",
      static_cast<unsigned long>(audio.startSuccesses),
      static_cast<unsigned long>(audio.startAttempts),
      static_cast<unsigned long>(audio.stopRequests),
      static_cast<unsigned long>(audio.blockCompletions),
      static_cast<unsigned long>(audio.blockRequests),
      static_cast<unsigned long>(audio.blockFailures),
      static_cast<unsigned long>(audio.abortedBlocks),
      static_cast<unsigned long>(audio.completedCaptures),
      static_cast<unsigned long>(audio.currentFrames),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void finalizeCaptureResult() {
  CaptureResult& result = captureResults[model.selectedTest];
  result.available = true;
  result.frames = audio.currentFrames;
  result.audioUs = audio.captureAudioUs;
  result.channels[0] = captureChannels[0];
  result.channels[1] = captureChannels[1];
  if (result.frames == 0) {
    result.channels[0].minimum = result.channels[0].maximum = 0;
    result.channels[1].minimum = result.channels[1].maximum = 0;
  }
  result.durationValid = result.frames >= kMinFrames;
  result.levelWarning = result.channels[0].nearFullScale != 0 ||
                        result.channels[1].nearFullScale != 0;

  const double effectiveRate = result.audioUs
      ? 1000000.0 * result.frames / result.audioUs : 0.0;
  Serial.printf(
      "AUDIO_RESULT test=%u frames=%lu duration_s=%.3f effective_rate=%.2f "
      "duration_valid=%s level_warning=%s\n",
      static_cast<unsigned>(model.selectedTest + 1),
      static_cast<unsigned long>(result.frames), result.audioUs / 1000000.0,
      effectiveRate, result.durationValid ? "yes" : "no",
      result.levelWarning ? "yes" : "no");
  for (uint8_t channel = 0; channel < 2; ++channel) {
    const ChannelStats& stats = result.channels[channel];
    const double rms = result.frames
        ? std::sqrt(static_cast<double>(stats.squares) / result.frames) : 0.0;
    const double dc = result.frames
        ? static_cast<double>(stats.sum) / result.frames : 0.0;
    const int32_t negativePeak = -static_cast<int32_t>(stats.minimum);
    const int32_t peak = negativePeak > stats.maximum ? negativePeak : stats.maximum;
    Serial.printf(
        "AUDIO_CHANNEL channel=%u min=%d max=%d peak=%ld rms=%.2f dc=%.2f "
        "near_full=%lu clipping_events=%lu\n",
        static_cast<unsigned>(channel), stats.minimum, stats.maximum,
        static_cast<long>(peak), rms, dc,
        static_cast<unsigned long>(stats.nearFullScale),
        static_cast<unsigned long>(stats.clippingEvents));
  }
}

void applyEvent(UiEvent event, uint8_t selectedTest, const char* source) {
  const UiModel before = model;
  const TransitionResult result = audio_probe::dispatch(model, event, selectedTest);
  if (result.accepted) ++acceptedEvents;
  else ++rejectedEvents;

  Serial.printf("EVENT source=%s event=%s accepted=%s from=%s to=%s selected=%u\n",
                source, eventName(event), result.accepted ? "yes" : "no",
                audio_probe::stateName(before.state),
                audio_probe::stateName(model.state), model.selectedTest + 1);

  const bool resumeCapture = before.state == UiState::StopConfirm &&
                             model.state == UiState::Capturing;
  runEffect(audio_probe::effectForTransition(before, model), resumeCapture);
  if (before.state != UiState::Result && model.state == UiState::Result) {
    finalizeCaptureResult();
  }

  if (result.changed) drawTransition(before);
  else drawFooterUpdate("footer_rejected");
  printStatus(source);
}

void serviceAudioCapture() {
  if (model.state != UiState::Capturing) return;

  if (!M5.Mic.isRunning()) {
    ++audio.blockFailures;
    Serial.println("AUDIO_BLOCK result=failed reason=mic_not_running");
    applyEvent(UiEvent::CompleteCapture, model.selectedTest, "audio_error");
    return;
  }

  if (audio.blockPending) {
    if (M5.Mic.isRecording()) return;
    audio.blockPending = false;
    audio.captureAudioUs += micros() - audio.blockStartedUs;
    const size_t sampleOffset = static_cast<size_t>(audio.currentFrames) * 2U;
    std::memcpy(audio.pcm + sampleOffset, audio.chunk,
                kChunkSamples * sizeof(int16_t));
    for (uint32_t frame = 0; frame < kChunkFrames; ++frame) {
      collectSample(captureChannels[0], audio.chunk[frame * 2U]);
      collectSample(captureChannels[1], audio.chunk[frame * 2U + 1U]);
    }
    ++audio.blockCompletions;
    ++audio.currentBlocks;
    audio.currentFrames += kChunkFrames;
    Serial.printf(
        "AUDIO_BLOCK result=complete capture_block=%u total_complete=%lu "
        "frames=%lu\n",
        static_cast<unsigned>(audio.currentBlocks),
        static_cast<unsigned long>(audio.blockCompletions),
        static_cast<unsigned long>(audio.currentFrames));

    const uint32_t displayedSecond = audio.currentFrames / kSampleRate;
    if (displayedSecond != audio.lastDisplayedSecond) {
      audio.lastDisplayedSecond = displayedSecond;
      const uint32_t renderStarted = micros();
      drawRow(model.selectedTest);
      drawFooter();
      finishRender(renderStarted, "capture_progress");
    }

    if (audio.currentFrames >= kMaxFrames) {
      ++audio.completedCaptures;
      applyEvent(UiEvent::CompleteCapture, model.selectedTest, "audio_complete");
      return;
    }
  }

  ++audio.blockRequests;
  if (!M5.Mic.record(audio.chunk, kChunkSamples, kSampleRate, true)) {
    ++audio.blockFailures;
    Serial.printf("AUDIO_BLOCK result=failed request=%lu\n",
                  static_cast<unsigned long>(audio.blockRequests));
    applyEvent(UiEvent::CompleteCapture, model.selectedTest, "audio_error");
    return;
  }
  audio.blockPending = true;
  audio.blockStartedUs = micros();
  Serial.printf("AUDIO_BLOCK result=requested capture_block=%u request=%lu\n",
                static_cast<unsigned>(audio.currentBlocks + 1),
                static_cast<unsigned long>(audio.blockRequests));
}

void pollTouch() {
  if (M5.Touch.getCount() == 0) {
    touchWasDown = false;
    return;
  }
  const auto detail = M5.Touch.getDetail(0);
  if (!detail.isPressed()) {
    touchWasDown = false;
    return;
  }
  if (touchWasDown) return;
  touchWasDown = true;

  ++physicalTouches;
  const int16_t height = rowHeight();
  if (detail.x < 0 || detail.y < 0 || detail.x >= M5.Display.width() ||
      detail.y >= contentHeight()) {
    ++rejectedEvents;
    Serial.printf("TOUCH_OUTSIDE x=%d y=%d\n", detail.x, detail.y);
    drawFooterUpdate("footer_outside");
    return;
  }

  const uint8_t row = static_cast<uint8_t>(detail.y / height);
  uint8_t zone = static_cast<uint8_t>((detail.x * 3) / M5.Display.width());
  if (zone > 2) zone = 2;
  const UiEvent event = audio_probe::eventForTap(model, row, zone);
  Serial.printf("TOUCH_PRESS number=%lu x=%d y=%d row=%u zone=%u\n",
                static_cast<unsigned long>(physicalTouches), detail.x, detail.y,
                row + 1, zone);
  applyEvent(event, row, "touch");
}

void serviceTouch() {
  const uint32_t now = millis();
  const int8_t intLevel = static_cast<int8_t>(digitalRead(23));
  if (lastTouchIntLevel < 0) {
    lastTouchIntLevel = intLevel;
    if (intLevel != 0) touchIntHighSince = now;
  } else if (intLevel != lastTouchIntLevel) {
    ++touchIntTransitions;
    lastTouchIntLevel = intLevel;
    if (intLevel != 0) touchIntHighSince = now;
  }

  if (intLevel == 0) {
    touchIntHighSince = 0;
    if (now - lastTouchPoll >= kTouchPollMs) {
      lastTouchPoll = now;
      ++touchReads;
      M5.update();
      pollTouch();
    }
    return;
  }

  ++touchIdleSkips;
  if (touchWasDown && touchIntHighSince != 0 &&
      now - touchIntHighSince >= kTouchReleaseMs) {
    touchWasDown = false;
    Serial.printf("TOUCH_RELEASE int23=1 reads=%lu\n",
                  static_cast<unsigned long>(touchReads));
  }
}

void printCommands() {
  Serial.println("COMMANDS: STATUS | LIST | NEXT | SELECT 1..6");
}

void executeCommand(char* value) {
  while (*value == ' ') ++value;
  for (char* p = value; *p; ++p) {
    if (*p >= 'a' && *p <= 'z') *p -= ('a' - 'A');
  }

  if (std::strcmp(value, "STATUS") == 0) {
    printStatus("serial");
    return;
  }
  if (std::strcmp(value, "LIST") == 0) {
    ++serialEvents;
    applyEvent(UiEvent::Back, model.selectedTest, "serial");
    return;
  }
  if (std::strcmp(value, "NEXT") == 0) {
    ++serialEvents;
    UiEvent event = UiEvent::None;
    switch (model.state) {
      case UiState::List: event = UiEvent::SelectTest; break;
      case UiState::Detail: event = UiEvent::Activate; break;
      case UiState::Capturing: event = UiEvent::Stop; break;
      case UiState::StopConfirm: event = UiEvent::ConfirmStop; break;
      case UiState::Result: event = UiEvent::Repeat; break;
    }
    applyEvent(event, model.selectedTest, "serial");
    return;
  }
  if (std::strncmp(value, "SELECT ", 7) == 0) {
    const int number = std::atoi(value + 7);
    ++serialEvents;
    applyEvent(UiEvent::SelectTest,
               number >= 1 && number <= audio_probe::kTestCount
                   ? static_cast<uint8_t>(number - 1)
                   : audio_probe::kTestCount,
               "serial");
    return;
  }
  printCommands();
}

void pollSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      command[commandLength] = '\0';
      if (commandLength) executeCommand(command);
      commandLength = 0;
    } else if (commandLength + 1 < sizeof(command)) {
      command[commandLength++] = c;
    }
  }
}

void heartbeat() {
  const uint32_t now = millis();
  if (now - lastHeartbeat < kHeartbeatMs) return;
  lastHeartbeat = now;
  printStatus("heartbeat");
}

bool initializeTouchController() {
  constexpr uint8_t kIoExpanderAddress = 0x43;
  constexpr uint8_t kOutputRegister = 0x05;
  constexpr uint8_t kTouchResetBit = 1U << 5;

  // A CPU-/Flash-Reset setzt den separat versorgten ST7123 nicht sicher
  // zurueck. Deshalb bekommt er bei jedem Programmstart einen definierten
  // Hardware-Reset, bevor M5Unified erneut an den Displaytreiber bindet.
  M5.Touch.end();
  const bool resetLow = M5.In_I2C.bitOff(kIoExpanderAddress, kOutputRegister,
                                         kTouchResetBit, 100000);
  M5.delay(20);
  const bool resetHigh = M5.In_I2C.bitOn(kIoExpanderAddress, kOutputRegister,
                                         kTouchResetBit, 100000);
  bool controllerReady = false;
  for (uint8_t attempt = 0; attempt < 70 && !controllerReady; ++attempt) {
    M5.delay(10);
    const uint8_t reg[2] = {0x00, 0x00};
    uint8_t firmware = 0;
    controllerReady = m5gfx::i2c::transactionWriteRead(
        static_cast<int>(M5.In_I2C.getPort()), 0x55, reg, sizeof(reg),
        &firmware, 1, 100000).has_value() && firmware != 0;
    if (controllerReady) lastTouchFirmware = firmware;
  }

  const bool driverReady = controllerReady && M5.Display.touch() &&
                           M5.Display.touch()->init();
  M5.Touch.begin(driverReady ? &M5.Display : nullptr);
  const bool unifiedReady = M5.Touch.isEnabled();
  touchWasDown = false;
  lastTouchIntLevel = static_cast<int8_t>(digitalRead(23));
  touchIntHighSince = lastTouchIntLevel == 0 ? 0 : millis();
  Serial.printf("TOUCH_RESET low=%s high=%s controller=%s fw=0x%02X driver=%s unified=%s\n",
                resetLow ? "ok" : "failed", resetHigh ? "ok" : "failed",
                controllerReady ? "ready" : "failed", lastTouchFirmware,
                driverReady ? "ready" : "failed",
                unifiedReady ? "ready" : "failed");
  return resetLow && resetHigh && controllerReady && driverReady && unifiedReady;
}

bool readTouchFirmware() {
  const uint8_t reg[2] = {0x00, 0x00};
  uint8_t firmware = 0;
  const bool ready = m5gfx::i2c::transactionWriteRead(
      static_cast<int>(M5.In_I2C.getPort()), 0x55, reg, sizeof(reg),
      &firmware, 1, 100000).has_value() && firmware != 0;
  lastTouchFirmware = ready ? firmware : 0;
  return ready;
}

void serviceTouchHealth() {
  const uint32_t now = millis();
  if (now - lastTouchHealthCheck < kTouchHealthMs || touchWasDown ||
      digitalRead(23) == 0) return;
  lastTouchHealthCheck = now;
  ++touchHealthChecks;
  if (readTouchFirmware()) return;

  ++touchHealthFailures;
  const uint32_t recoveryStarted = millis();
  Serial.printf("TOUCH_HEALTH result=failed check=%lu action=reset\n",
                static_cast<unsigned long>(touchHealthChecks));
  const bool recovered = initializeTouchController();
  if (recovered) ++touchRecoveries;
  Serial.printf("TOUCH_HEALTH result=%s recovery_ms=%lu\n",
                recovered ? "recovered" : "recovery_failed",
                static_cast<unsigned long>(millis() - recoveryStarted));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  auto config = M5.config();
  config.clear_display = true;
  config.internal_imu = false;
  M5.begin(config);
  const bool touchReady = initializeTouchController();
  const bool audioReady = prepareAudioWithoutStartingMicrophone();

  drawAll();
  Serial.println("AUDIO_PROBE mode=FULL_CAPTURE_AND_ANALYSIS");
  Serial.println("DISABLED speaker sd direct_touch_fallback");
  Serial.println("TOUCH_POLICY irq_reads health_2s reset_on_failed_health");
  Serial.printf("TOUCH_READY enabled=%s driver=%s display=%dx%d rotation=%u\n",
                M5.Touch.isEnabled() ? "yes" : "no",
                M5.Display.touch() ? "present" : "missing", M5.Display.width(),
                M5.Display.height(),
                static_cast<unsigned>(M5.Display.getRotation()));
  if (!touchReady) Serial.println("TOUCH_ERROR boot_reset_failed");
  if (!audioReady) Serial.println("AUDIO_ERROR passive_preparation_failed");
  printCommands();
  printStatus("boot");
}

void loop() {
  serviceTouch();
  serviceTouchHealth();
  pollSerial();
  serviceAudioCapture();
  heartbeat();
  M5.delay(1);
}
