#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "measurement_snapshot_types.h"

namespace digifant::ui {

enum class DisplayTab : uint8_t { Compact = 0, List = 1, System = 2, Traces = 3 };

enum class DisplayFieldStyle : uint8_t { Normal, Caution, RawUnknown, Invalid };
enum class EngineOperatingState : uint8_t { Unknown, Stopped, Running };
enum class CompactFieldAvailability : uint8_t { Available, EngineStateUnknown, EngineStopped };

inline DisplayFieldStyle displayFieldStyle(const MeasurementField& field,
                                           SignalValidity snapshotValidity) noexcept {
  if (snapshotValidity != SignalValidity::Valid || field.status == FieldStatus::Unavailable ||
      field.status == FieldStatus::Invalid) {
    return DisplayFieldStyle::Invalid;
  }
  if (field.status == FieldStatus::RawOnly || field.formulaEvidence == EvidenceGrade::Unknown) {
    return DisplayFieldStyle::RawUnknown;
  }
  if (field.formulaEvidence == EvidenceGrade::Inferred ||
      field.formulaEvidence == EvidenceGrade::Experimental) {
    return DisplayFieldStyle::Caution;
  }
  return DisplayFieldStyle::Normal;
}

inline bool displayFieldBelongsToSnapshot(const MeasurementField& field,
                                          const MeasurementSnapshot& snapshot) noexcept {
  return field.sessionEpoch == snapshot.sessionEpoch &&
         field.transportGeneration == snapshot.transportGeneration;
}

inline DisplayFieldStyle displayFieldStyle(const MeasurementField& field,
                                           const MeasurementSnapshot& snapshot) noexcept {
  if (!displayFieldBelongsToSnapshot(field, snapshot)) return DisplayFieldStyle::Invalid;
  return displayFieldStyle(field, snapshot.validity);
}

struct DisplayScopeSample {
  uint16_t rpm = 0;
  uint8_t coolantRaw = 0;
  uint8_t iatRaw = 0;
  uint8_t supplyRaw = 0;
  uint8_t loadRaw = 0;
  uint8_t g69Raw = 0;
  uint8_t injectionRaw = 0;
  uint8_t lambdaRaw = 0;
  uint32_t sequence = 0;
};

template <std::size_t Capacity>
class DisplayScopeRing {
 public:
  static_assert(Capacity > 0, "scope ring must have capacity");

  void push(const DisplayScopeSample& sample) noexcept {
    samples_[head_] = sample;
    head_ = (head_ + 1U) % Capacity;
    if (count_ < Capacity) ++count_;
  }

  std::size_t size() const noexcept { return count_; }
  std::size_t capacity() const noexcept { return Capacity; }

  const DisplayScopeSample& chronological(std::size_t index) const noexcept {
    static const DisplayScopeSample empty{};
    if (index >= count_) return empty;
    const std::size_t first = count_ == Capacity ? head_ : 0U;
    return samples_[(first + index) % Capacity];
  }

 private:
  std::array<DisplayScopeSample, Capacity> samples_{};
  std::size_t head_ = 0;
  std::size_t count_ = 0;
};

class DisplayUiModel {
 public:
  static constexpr std::size_t kScopeCapacity = 240;
  static constexpr uint32_t kSamplePeriodMs = 100;

  void accept(const MeasurementSnapshot& snapshot) noexcept {
    if (hasSnapshot_ && snapshot.lastRxSequence == snapshot_.lastRxSequence &&
        snapshot.transportGeneration == snapshot_.transportGeneration &&
        snapshot.sessionEpoch == snapshot_.sessionEpoch &&
        snapshot.k409Connected == snapshot_.k409Connected &&
        snapshot.kwpConnected == snapshot_.kwpConnected &&
        snapshot.ecuDataValid == snapshot_.ecuDataValid &&
        snapshot.validity == snapshot_.validity &&
        snapshot.frameDrops == snapshot_.frameDrops &&
        snapshot.faultCount == snapshot_.faultCount) {
      snapshot_ = snapshot;
      return;
    }
    snapshot_ = snapshot;
    hasSnapshot_ = true;
    dirty_ = true;
  }

  void sampleIfDue(uint32_t nowMs) noexcept {
    if (!hasSnapshot_ || paused_ || nowMs - lastSampleMs_ < kSamplePeriodMs) return;
    lastSampleMs_ = nowMs;
    const MeasurementField* load = field(3, 2);
    const MeasurementField* g69 = field(3, 3);
    const MeasurementField* injection = field(2, 2);
    const MeasurementField* lambda = field(1, 3);
    const MeasurementField* rpm = preferredRpmField();
    scope_.push(DisplayScopeSample{
        static_cast<uint16_t>(rpm != nullptr ? rpm->decodedValue : 0.0f),
        snapshot_.coolantRaw,
        snapshot_.iatRaw,
        snapshot_.batteryRaw,
        static_cast<uint8_t>(load != nullptr ? load->raw : 0),
        g69 != nullptr ? g69->raw : snapshot_.g69Raw,
        static_cast<uint8_t>(injection != nullptr ? injection->raw : 0),
        static_cast<uint8_t>(lambda != nullptr ? lambda->raw : 0),
        snapshot_.lastRxSequence});
    if (scope_.size() == 1 || snapshot_.lastRxSequence != lastSampleSequence_) dirty_ = true;
    lastSampleSequence_ = snapshot_.lastRxSequence;
  }

  const MeasurementSnapshot& snapshot() const noexcept { return snapshot_; }
  const DisplayScopeRing<kScopeCapacity>& scope() const noexcept { return scope_; }
  bool hasSnapshot() const noexcept { return hasSnapshot_; }
  bool dirty() const noexcept { return dirty_; }
  void clearDirty() noexcept { dirty_ = false; }

  DisplayTab tab() const noexcept { return tab_; }
  void setTab(DisplayTab tab) noexcept {
    if (tab_ == tab) return;
    tab_ = tab;
    dirty_ = true;
  }

  void nextTab() noexcept {
    setTab(static_cast<DisplayTab>((static_cast<uint8_t>(tab_) + 1U) % 4U));
  }

  void setListPage(uint8_t page) noexcept {
    const uint8_t bounded = page > 1 ? 1 : page;
    if (listPage_ == bounded) return;
    listPage_ = bounded;
    dirty_ = true;
  }
  uint8_t listPage() const noexcept { return listPage_; }

  void toggleScopePause() noexcept {
    paused_ = !paused_;
    dirty_ = true;
  }
  bool scopePaused() const noexcept { return paused_; }

  const MeasurementField* field(uint8_t group, uint8_t zone) const noexcept {
    for (uint8_t i = 0; i < snapshot_.fieldCount && i < MeasurementSnapshot::kFieldCount; ++i) {
      const auto& candidate = snapshot_.fields[i];
      if (candidate.group == group && candidate.zone == zone) return &candidate;
    }
    return nullptr;
  }

  const MeasurementField* preferredRpmField() const noexcept {
    if (snapshot_.validity != SignalValidity::Valid) return nullptr;
    const MeasurementField* newest = nullptr;
    for (uint8_t i = 0; i < snapshot_.fieldCount && i < MeasurementSnapshot::kFieldCount; ++i) {
      const auto& candidate = snapshot_.fields[i];
      if (candidate.semantic != FieldSemantic::Rpm || candidate.status != FieldStatus::Decoded ||
          candidate.formulaEvidence != EvidenceGrade::Reference ||
          !displayFieldBelongsToSnapshot(candidate, snapshot_)) {
        continue;
      }
      if (newest == nullptr || candidate.rxSequence > newest->rxSequence) newest = &candidate;
    }
    return newest;
  }

  EngineOperatingState engineOperatingState() const noexcept {
    const MeasurementField* rpm = preferredRpmField();
    if (rpm == nullptr) return EngineOperatingState::Unknown;
    return rpm->decodedValue > 0.0f ? EngineOperatingState::Running
                                   : EngineOperatingState::Stopped;
  }

  CompactFieldAvailability compactAvailability(FieldSemantic semantic) const noexcept {
    if (semantic != FieldSemantic::MotorLoad && semantic != FieldSemantic::InjectionTime)
      return CompactFieldAvailability::Available;
    switch (engineOperatingState()) {
      case EngineOperatingState::Running: return CompactFieldAvailability::Available;
      case EngineOperatingState::Stopped: return CompactFieldAvailability::EngineStopped;
      default: return CompactFieldAvailability::EngineStateUnknown;
    }
  }

 private:
  MeasurementSnapshot snapshot_{};
  DisplayScopeRing<kScopeCapacity> scope_{};
  DisplayTab tab_ = DisplayTab::Compact;
  uint8_t listPage_ = 0;
  uint32_t lastSampleMs_ = 0;
  uint32_t lastSampleSequence_ = 0;
  bool hasSnapshot_ = false;
  bool paused_ = false;
  bool dirty_ = true;
};

}  // namespace digifant::ui
