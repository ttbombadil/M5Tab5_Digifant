#pragma once

#include <cstdint>
#include <limits>

namespace digifant::k409 {

inline constexpr std::uint16_t vendor_id = 0x0403;
inline constexpr std::uint16_t product_id = 0x6001;

constexpr bool matches(std::uint16_t vid, std::uint16_t pid) noexcept
{
  return vid == vendor_id && pid == product_id;
}

class GenerationTracker {
public:
  constexpr std::uint32_t connected(std::uint16_t vid, std::uint16_t pid) noexcept
  {
    if (!matches(vid, pid) || generation_ == std::numeric_limits<std::uint32_t>::max()) {
      return 0;
    }
    return ++generation_;
  }

  constexpr std::uint32_t current() const noexcept { return generation_; }

private:
  std::uint32_t generation_ = 0;
};

}  // namespace digifant::k409
