// Common host-side interface implemented by the CPU and GPU renderers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mathcore.h"

namespace rasty {

struct Mesh;

// Per-frame settings shared by both implementations. Matrices follow mathcore's
// column-major convention; MVP is projection * view * model.
struct RenderSettings {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  math::Mat4f model = math::identity();
  math::Mat4f view = math::identity();
  math::Mat4f projection = math::identity();
  math::Vec3f light_direction = math::vec3(0.0f, 0.0f, 1.0f);
  math::Vec3f clear_color = math::vec3(0.0f, 0.0f, 0.0f);
  float ambient = 0.2f;
  float diffuse = 0.8f;
};

// Pixels are row-major and packed as 0xAABBGGRR, matching math::to_rgba8().
struct FrameBuffer {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint32_t> rgba;

  bool valid() const {
    return rgba.size() == static_cast<std::size_t>(width) * height;
  }
};

// Rasterization contract for implementations:
// - transform and clip mesh triangles using the shared math conventions;
// - sample pixel centers and use the top-left coverage rule;
// - depth-test fragments, resolving equal quantized depths by lower triangle ID;
// - interpolate vertex normals/colors perspective-correctly and write packed RGBA.
//
// GPU implementations may stage data through device buffers internally, but
// expose the same host-side result through this interface.
class Renderer {
 public:
  virtual ~Renderer() = default;

  virtual FrameBuffer render(const Mesh& mesh,
                             const RenderSettings& settings) const = 0;
};

}  // namespace rasty
