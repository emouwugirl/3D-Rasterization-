// Byte order of the machine running the converter (internal header).
#pragma once

#include <cstdint>
#include <cstring>

namespace rasty::convert::detail {

inline bool host_is_little_endian() {
  const std::uint16_t one = 1;
  std::uint8_t first = 0;
  std::memcpy(&first, &one, 1);
  return first == 1;
}

}  // namespace rasty::convert::detail
