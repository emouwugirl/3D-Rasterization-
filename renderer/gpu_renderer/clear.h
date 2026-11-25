#pragma once

#include <cstddef>
#include <cstdint>

namespace rasty::gpu {

// fill every pixel with a color for newly allocated GPU mem for the buffer
void clear(std::uint32_t* pixels, std::size_t count, std::uint32_t color);

} 
