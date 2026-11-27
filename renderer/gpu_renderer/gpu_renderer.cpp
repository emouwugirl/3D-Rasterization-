#include "gpu_renderer.h"

#include <cmath>
#include <stdexcept>

#include "device_buffer.h"
#include "rasty/bin_format.hpp"
#include "clear.h"
#include "transform.h"
#include "raster.h"
#include "shade.h"

namespace rasty {

FrameBuffer GpuRenderer::render(const Mesh& mesh, const RenderSettings& settings) const {
  if (settings.width > 16384 || settings.height > 16384) // limit pulled from cpu renderer
    throw std::invalid_argument("GpuRenderer: image sides are limited to 16384 pixels");
  FrameBuffer frame{settings.width, settings.height, {}};
  if (frame.width == 0 || frame.height == 0) return frame;
  if (mesh.normals.size() != mesh.positions.size())
    throw std::invalid_argument("GpuRenderer: each vertex needs a normal");

  const auto color = settings.clear_color;
  if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z))
    throw std::invalid_argument("Clear color must be finite");

  const std::size_t count = std::size_t(frame.width) * frame.height;
  frame.rgba.resize(count);
  gpu::DeviceBuffer<std::uint32_t> pixels(count);
  gpu::clear(pixels.data(), count, math::to_rgba8(color));

  // prepare the matrices for all the vertices
  auto constants = pipeline::make_frame_constants(
      settings.model, settings.view, settings.projection,
      static_cast<std::int32_t>(frame.width), static_cast<std::int32_t>(frame.height));

  // upload cpu to gpu the pos and normals inputs
  const std::size_t vertex_count = mesh.vertex_count();
  gpu::DeviceBuffer<rasty::Vec3f> positions(vertex_count);
  gpu::DeviceBuffer<rasty::Vec3f> normals(vertex_count);
  positions.upload(mesh.positions.data());
  normals.upload(mesh.normals.data());

  // outputs
  gpu::DeviceBuffer<pipeline::ScreenVertex> vertices(vertex_count);
  gpu::DeviceBuffer<math::Vec3f> world_normals(vertex_count);
  gpu::transform(constants, positions.data(), normals.data(), vertex_count, vertices.data(), world_normals.data());

  gpu::DeviceBuffer<Triangle> triangles(mesh.triangle_count());
  triangles.upload(mesh.triangles.data());
  gpu::DeviceBuffer<unsigned long long> visibility(count);

  gpu::check_cuda(cudaMemset(visibility.data(), 0xff, count * sizeof(unsigned long long)), "clearing the visibility buffer");

  gpu::raster(constants, vertices.data(), triangles.data(), mesh.triangle_count(), visibility.data());

  gpu::DeviceBuffer<rasty::Vec3f> colors(mesh.colors.size());
  colors.upload(mesh.colors.data());

  gpu::shade(constants, visibility.data(), triangles.data(), vertices.data(), world_normals.data(), colors.data(), pixels.data());

  pixels.download(frame.rgba.data());
  return frame;
}

} 
