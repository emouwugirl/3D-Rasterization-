// CPU renderer: runs the shared stages from pipeline.h in plain loops.
//
//   clear   for every pixel     depth key = "no triangle"
//   vertex  for every vertex    vertex_stage() and world_normal()
//   raster  for every triangle  setup_triangle(), then raster_triangle() right away
//   shade   for every pixel     shade_sample() for the triangle that won the pixel
//
// Each loop is split across a thread pool. Two threads can draw over the same pixel, so the depth test
// is an atomic minimum. The smallest key wins no matter the order, so the image is the same for any
// number of threads.
#pragma once

#include <cstdint>
#include <memory>

#include "rasty/bin_format.hpp"
#include "renderer.h"

namespace rasty {

class ThreadPool;

struct CpuRendererOptions {
  unsigned threads = 0;         // 0 = one per CPU core, 1 = single-threaded
  bool cull_back_faces = true;  // skip triangles facing away from the camera
  math::Vec3f albedo = math::vec3(0.8f, 0.8f, 0.8f);  // color for meshes without vertex colors
};

// How long each stage of the last frame took, and what it drew.
struct CpuFrameStats {
  double clear_ms = 0.0;
  double vertex_ms = 0.0;
  double raster_ms = 0.0;  // setup + raster
  double shade_ms = 0.0;
  double total_ms = 0.0;
  std::uint64_t triangles = 0;             // in the mesh
  std::uint64_t triangles_rasterized = 0;  // not clipped, and had at least one pixel to test
  std::uint64_t triangles_clipped = 0;     // went through the clipper
  std::uint64_t pixels_covered = 0;        // pixels that show a triangle
  unsigned threads = 1;
};

class CpuRenderer final : public Renderer {
 public:
  explicit CpuRenderer(CpuRendererOptions options = {});
  ~CpuRenderer() override;

  // The shared interface. The mesh should pass validate_mesh() (every mesh from obj2bin does).
  FrameBuffer render(const Mesh& mesh, const RenderSettings& settings) const override;

  // Same as render(), but draws into `out` so its memory is reused from frame to frame.
  void render_into(const Mesh& mesh, const RenderSettings& settings, FrameBuffer& out) const;

  const CpuFrameStats& last_stats() const;
  unsigned threads() const;

 private:
  // Buffers kept between frames so a frame doesn't allocate. render() is const in the shared interface,
  // so they sit behind a pointer. Don't call render() on one CpuRenderer from two threads at once.
  struct Scratch;

  CpuRendererOptions options_;
  std::unique_ptr<ThreadPool> pool_;
  std::unique_ptr<Scratch> scratch_;
};

}  // namespace rasty
