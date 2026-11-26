// read triangle's three vertex indices
// look up its transofmred vertices
// read if necessary
// set up triangle edges and pixel bounds
// check covered pixels & update visibility

#include "raster.h"
#include "cuda_error.h"
#include "rasty/bin_format.hpp"

#include <algorithm>
#include <cuda_runtime.h>
#include <cstdint>

namespace rasty::gpu {
namespace {

// the smaller depth/triangle wins
struct CheckDepth {
  unsigned long long* visibility; // gpu array with each pixel's winning depth and triangle 

  __device__ void operator()(std::int64_t pixel, std::uint64_t key) const {
    atomicMin(&visibility[pixel], static_cast<unsigned long long>(key)); //when multiple threads
  }
};

// the serious
__global__ void raster_kernel(pipeline::FrameConstants constants,
                              const pipeline::ScreenVertex* vertices,
                              const Triangle* triangles,
                              std::size_t triangle_count,
                              unsigned long long* visibility) {
  const std::size_t start = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
  const std::size_t stride = std::size_t(gridDim.x) * blockDim.x;
  const CheckDepth check_depth{visibility};

  for (std::size_t i = start; i < triangle_count; i += stride) {
    const Triangle& triangle = triangles[i];
    const auto& a = vertices[triangle.v[0]];
    const auto& b = vertices[triangle.v[1]];
    const auto& c = vertices[triangle.v[2]];
    const auto id = static_cast<std::uint32_t>(i);
    pipeline::TriSetup setup;

    // case: all three vertices already have screen coordinates
    if (a.projected && b.projected && c.projected) {
      if (pipeline::setup_triangle(constants, a.pos, b.pos, c.pos,
                                   a.depth, b.depth, c.depth, id, setup)) {
        pipeline::raster_triangle(setup, constants.width, check_depth);
      }
      continue;
    }

    // otherwise clip, then call raster 
    if (math::all_outside_one_plane(a.clip, b.clip, c.clip)) continue;
    const auto polygon = pipeline::clip_triangle(constants, a.clip, b.clip, c.clip);
    math::IVec2 positions[8];
    float depths[8];
    for (int v = 0; v < polygon.count; ++v)
      pipeline::project(constants, polygon.v[v], positions[v], depths[v]);

    // clipped pieces keep the OG triangle ID for shading later
    for (int v = 1; v + 1 < polygon.count; ++v) {
      if (pipeline::setup_triangle(constants, positions[0], positions[v], positions[v + 1],
                                   depths[0], depths[v], depths[v + 1], id, setup)) {
        pipeline::raster_triangle(setup, constants.width, check_depth);
      }
    }
  }
}

} 

void raster(const pipeline::FrameConstants& constants,
            const pipeline::ScreenVertex* vertices,
            const Triangle* triangles,
            std::size_t triangle_count,
            unsigned long long* visibility) {
  if (triangle_count == 0) return; //no triangle
  if (triangle_count > math::kNoTriangle) // check triangles fit in visibility key
    throw std::length_error("Too many triangles for the visibility buffer");

  constexpr unsigned threads = 256;
  const auto blocks = static_cast<unsigned>( // calc number of blocks
      std::min((triangle_count - 1) / threads + 1, std::size_t(65535)));
  raster_kernel<<<blocks, threads>>>(constants, vertices, triangles, triangle_count, visibility);
  check_cuda(cudaGetLastError(), "Launch raster kernel");
}

}  
