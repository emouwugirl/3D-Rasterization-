#include "clear.h"
#include "cuda_error.h"

#include <algorithm>

//initializes with a color for the framebuffer to avoid background contaning garbage or wtv

namespace rasty::gpu {
namespace {

__global__ void clear_kernel(std::uint32_t* pixels, std::size_t count, std::uint32_t color) {
  const std::size_t start = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
  const std::size_t stride = std::size_t(gridDim.x) * blockDim.x;
  for (std::size_t i = start; i < count; i += stride)
    pixels[i] = color;
}

}

void clear(std::uint32_t* pixels, std::size_t count, std::uint32_t color) {
  if (count == 0) return;
  constexpr unsigned threads = 256;
  const auto blocks = static_cast<unsigned>(std::min((count - 1) / threads + 1, std::size_t(65535)));
  clear_kernel<<<blocks, threads>>>(pixels, count, color);
  check_cuda(cudaGetLastError(), "Launch clear kernel");
}

} 
