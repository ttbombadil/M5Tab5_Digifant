#pragma once

#include <cstdint>

namespace digifant::application {

// Recovery timing taken from the Known-Good prototype. This policy owns no
// transport or KWP state; the single protocol owner asks it only when another
// complete session attempt may start.
class KwpReconnectPolicy {
 public:
  static constexpr uint32_t kRetryIntervalMs = 6000;
  static constexpr uint32_t kSessionStallMs = 4000;

  bool shouldAttempt(bool transport_connected, uint32_t generation,
                     uint32_t now_ms) noexcept {
    if (!transport_connected || generation == 0) return false;
    if (generation != generation_) {
      generation_ = generation;
      have_finished_attempt_ = false;
    }
    return !have_finished_attempt_ ||
           static_cast<uint32_t>(now_ms - last_attempt_finished_ms_) >= kRetryIntervalMs;
  }

  void attemptFinished(uint32_t now_ms) noexcept {
    last_attempt_finished_ms_ = now_ms;
    have_finished_attempt_ = true;
  }

  static bool sessionStalled(uint32_t now_ms, uint32_t last_progress_ms) noexcept {
    return static_cast<uint32_t>(now_ms - last_progress_ms) >= kSessionStallMs;
  }

 private:
  uint32_t generation_ = 0;
  uint32_t last_attempt_finished_ms_ = 0;
  bool have_finished_attempt_ = false;
};

}  // namespace digifant::application
