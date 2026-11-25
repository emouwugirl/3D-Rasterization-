#include "gpu_renderer.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "clear.h"
#include "device_buffer.h"

namespace rasty {

FrameBuffer GpuRenderer::render(const Mesh&, const RenderSettings& settings) const {
  FrameBuffer frame{settings.width, settings.height, {}};
  if (frame.width == 0 || frame.height == 0) return frame;
  if (std::size_t(frame.width) > std::numeric_limits<std::size_t>::max() / frame.height)
    throw std::length_error("Framebuffer dimensions are too large");

  const auto color = settings.clear_color;
  if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z))
    throw std::invalid_argument("Clear color must be finite");

  const std::size_t count = std::size_t(frame.width) * frame.height;
  frame.rgba.resize(count);
  gpu::DeviceBuffer<std::uint32_t> pixels(count);
  gpu::clear(pixels.data(), count, math::to_rgba8(color));
  pixels.download(frame.rgba.data());
  return frame;
}

} 
