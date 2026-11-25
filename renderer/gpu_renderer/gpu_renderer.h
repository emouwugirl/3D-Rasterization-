#pragma once

#include "renderer.h"

namespace rasty {

// currently returns a GPU-cleared frame
class GpuRenderer final : public Renderer {
 public:
  FrameBuffer render(const Mesh& mesh, const RenderSettings& settings) const override;
};

} 
