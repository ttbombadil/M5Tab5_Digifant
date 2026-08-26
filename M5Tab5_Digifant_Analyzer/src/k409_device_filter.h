#pragma once

#include <cstdint>

namespace digifant::k409 {

inline constexpr std::uint16_t vendor_id = 0x0403;
inline constexpr std::uint16_t product_id = 0x6001;

constexpr bool matches(std::uint16_t vid, std::uint16_t pid) noexcept
{
  return vid == vendor_id && pid == product_id;
}

}  // namespace digifant::k409
