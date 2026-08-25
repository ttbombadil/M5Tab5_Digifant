#pragma once

#include <cstdint>

namespace digifant::transport {

enum class OperationState : uint8_t { Active, Retiring, Retired };
enum class OperationStatus : uint8_t { Success, Error, Aborted, Unknown };

struct OperationToken {
  uint32_t generation = 0;
  uint64_t operationId = 0;
  uint8_t kind = 0;
  friend constexpr bool operator==(OperationToken a, OperationToken b) noexcept
  {
    return a.generation == b.generation && a.operationId == b.operationId && a.kind == b.kind;
  }
};

class OperationLifecycle {
public:
  constexpr bool accept(OperationToken token) noexcept
  {
    if (state_ != OperationState::Retired || token.operationId == 0 || token == retiredToken_) return false;
    token_ = token;
    state_ = OperationState::Active;
    return true;
  }

  constexpr bool timeout() noexcept
  {
    if (state_ != OperationState::Active) return false;
    state_ = OperationState::Retiring;
    return true;
  }

  constexpr bool terminal(OperationToken token, OperationStatus status) noexcept
  {
    if (state_ != OperationState::Active && state_ != OperationState::Retiring) return false;
    if (!(token == token_)) return false;
    status_ = status;
    retiredToken_ = token_;
    state_ = OperationState::Retired;
    return true;
  }

  constexpr OperationState state() const noexcept { return state_; }
  constexpr OperationStatus status() const noexcept { return status_; }
  constexpr bool quiescent() const noexcept { return state_ == OperationState::Retired; }

private:
  OperationToken token_{};
  OperationToken retiredToken_{};
  OperationStatus status_ = OperationStatus::Unknown;
  OperationState state_ = OperationState::Retired;
};

}  // namespace digifant::transport
