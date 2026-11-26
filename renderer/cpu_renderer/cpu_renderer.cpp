#include "cpu_renderer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <vector>

#include "pipeline.h"
#include "thread_pool.h"

namespace rasty {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// How many items each thread grabs at a time.
constexpr std::size_t kVerticesPerPiece = 8192;
constexpr std::size_t kTrianglesPerPiece = 2048;
constexpr std::size_t kPixelsPerPiece = 16384;

// Biggest image side we accept, so fixed-point screen coordinates stay well inside int32.
constexpr std::uint32_t kMaxImageSide = 16384;

// The depth test. Keeps the smaller key; safe when several threads hit the same pixel
// (the CPU version of CUDA's atomicMin).
struct DepthTest {
  std::atomic<std::uint64_t>* depth_id;

  void operator()(std::int64_t pixel, std::uint64_t key) const {
    std::atomic<std::uint64_t>& slot = depth_id[pixel];
    std::uint64_t current = slot.load(std::memory_order_relaxed);
    while (key < current && !slot.compare_exchange_weak(current, key, std::memory_order_relaxed)) {
      // another thread changed the pixel; `current` now holds its key, so compare again
    }
  }
};

struct Counters {
  std::atomic<std::uint64_t> rasterized{0};
  std::atomic<std::uint64_t> clipped{0};
  std::atomic<std::uint64_t> covered{0};
};

// Setup + raster for triangles [begin, end).
void raster_triangles(const pipeline::FrameConstants& k, const Mesh& mesh, const pipeline::ScreenVertex* sv,
                      DepthTest depth_test, std::size_t begin, std::size_t end, Counters& counts) {
  std::uint64_t rasterized = 0, clipped = 0;
  pipeline::TriSetup t;
  for (std::size_t i = begin; i < end; ++i) {
    const Triangle& tri = mesh.triangles[i];
    const pipeline::ScreenVertex& a = sv[tri.v[0]];
    const pipeline::ScreenVertex& b = sv[tri.v[1]];
    const pipeline::ScreenVertex& c = sv[tri.v[2]];
    const auto id = static_cast<std::uint32_t>(i);

    // Usual case: all three corners already have screen positions.
    if (a.projected && b.projected && c.projected) {
      if (pipeline::setup_triangle(k, a.pos, b.pos, c.pos, a.depth, b.depth, c.depth, id, t)) {
        pipeline::raster_triangle(t, k.width, depth_test);
        ++rasterized;
      }
      continue;
    }

    // Rare case: part of the triangle is behind the camera or far off screen. Clip it, then draw the
    // pieces as a fan of triangles that all keep the original id.
    if (math::all_outside_one_plane(a.clip, b.clip, c.clip)) continue;
    ++clipped;
    const pipeline::ClipPolygon poly = pipeline::clip_triangle(k, a.clip, b.clip, c.clip);
    math::IVec2 pos[8];
    float depth[8];
    for (int v = 0; v < poly.count; ++v) pipeline::project(k, poly.v[v], pos[v], depth[v]);
    for (int v = 1; v + 1 < poly.count; ++v) {
      if (pipeline::setup_triangle(k, pos[0], pos[v], pos[v + 1], depth[0], depth[v], depth[v + 1], id, t)) {
        pipeline::raster_triangle(t, k.width, depth_test);
      }
    }
  }
  counts.rasterized += rasterized;
  counts.clipped += clipped;
}

}  // namespace

struct CpuRenderer::Scratch {
  std::vector<pipeline::ScreenVertex> vertices;           // one per mesh vertex
  std::vector<math::Vec3f> normals;                       // world-space normal per mesh vertex
  std::unique_ptr<std::atomic<std::uint64_t>[]> depth_id; // one key per pixel: depth << 32 | id
  std::size_t depth_size = 0;
  CpuFrameStats stats;
};

CpuRenderer::CpuRenderer(CpuRendererOptions options)
    : options_(options),
      pool_(std::make_unique<ThreadPool>(options.threads)),
      scratch_(std::make_unique<Scratch>()) {}

CpuRenderer::~CpuRenderer() = default;

unsigned CpuRenderer::threads() const { return pool_->size(); }

const CpuFrameStats& CpuRenderer::last_stats() const { return scratch_->stats; }

FrameBuffer CpuRenderer::render(const Mesh& mesh, const RenderSettings& settings) const {
  FrameBuffer frame;
  render_into(mesh, settings, frame);
  return frame;
}

void CpuRenderer::render_into(const Mesh& mesh, const RenderSettings& settings, FrameBuffer& out) const {
  const Clock::time_point frame_start = Clock::now();
  if (settings.width > kMaxImageSide || settings.height > kMaxImageSide) {
    throw std::invalid_argument("CpuRenderer: image sides are limited to 16384 pixels");
  }

  Scratch& m = *scratch_;
  CpuFrameStats& stats = m.stats;
  stats = CpuFrameStats{};
  stats.threads = pool_->size();
  stats.triangles = mesh.triangle_count();

  const std::size_t width = settings.width;
  const std::size_t height = settings.height;
  const std::size_t pixels = width * height;
  out.width = settings.width;
  out.height = settings.height;
  out.rgba.resize(pixels);
  if (pixels == 0) return;

  pipeline::FrameConstants k = pipeline::make_frame_constants(
      settings.model, settings.view, settings.projection, static_cast<std::int32_t>(width),
      static_cast<std::int32_t>(height));
  k.light_direction = math::normalize_or(settings.light_direction, math::vec3(0.0f, 0.0f, 1.0f));
  k.clear_color = settings.clear_color;
  k.ambient = settings.ambient;
  k.diffuse = settings.diffuse;
  k.albedo = options_.albedo;
  k.cull_back_faces = options_.cull_back_faces;

  // Grow the buffers if needed (they're kept for the next frame).
  m.vertices.resize(mesh.vertex_count());
  m.normals.resize(mesh.vertex_count());
  if (m.depth_size != pixels) {
    m.depth_id = std::make_unique<std::atomic<std::uint64_t>[]>(pixels);
    m.depth_size = pixels;
  }
  pipeline::ScreenVertex* sv = m.vertices.data();
  math::Vec3f* normals = m.normals.data();
  std::atomic<std::uint64_t>* depth_id = m.depth_id.get();
  std::uint32_t* rgba = out.rgba.data();
  const std::size_t rows_per_piece = std::max<std::size_t>(1, kPixelsPerPiece / width);
  Counters counts;

  // 1. Clear. "No triangle" is the biggest possible key, so any triangle beats it.
  Clock::time_point start = Clock::now();
  pool_->parallel_for(height, rows_per_piece, [&](std::size_t y0, std::size_t y1) {
    for (std::size_t p = y0 * width; p < y1 * width; ++p) {
      depth_id[p].store(math::kNoDepthId, std::memory_order_relaxed);
    }
  });
  stats.clear_ms = ms_since(start);

  // 2. Vertices.
  start = Clock::now();
  pool_->parallel_for(mesh.vertex_count(), kVerticesPerPiece, [&](std::size_t begin, std::size_t end) {
    for (std::size_t v = begin; v < end; ++v) {
      const Vec3f& p = mesh.positions[v];
      const Vec3f& n = mesh.normals[v];
      sv[v] = pipeline::vertex_stage(k, math::vec4(p.x, p.y, p.z, 1.0f));
      normals[v] = pipeline::world_normal(k, math::vec3(n.x, n.y, n.z));
    }
  });
  stats.vertex_ms = ms_since(start);

  // 3. Triangles: setup and raster together.
  start = Clock::now();
  const DepthTest depth_test{depth_id};
  pool_->parallel_for(mesh.triangle_count(), kTrianglesPerPiece, [&](std::size_t begin, std::size_t end) {
    raster_triangles(k, mesh, sv, depth_test, begin, end, counts);
  });
  stats.raster_ms = ms_since(start);

  // 4. Shade each pixel once, using the triangle that won it.
  start = Clock::now();
  const std::uint32_t clear_rgba = math::to_rgba8(k.clear_color);
  pool_->parallel_for(height, rows_per_piece, [&](std::size_t y0, std::size_t y1) {
    std::uint64_t covered = 0;
    for (std::size_t y = y0; y < y1; ++y) {
      for (std::size_t x = 0; x < width; ++x) {
        const std::size_t p = y * width + x;
        const std::uint64_t key = depth_id[p].load(std::memory_order_relaxed);
        if (key == math::kNoDepthId) {
          rgba[p] = clear_rgba;
          continue;
        }
        ++covered;

        // Look up the winning triangle's three corners.
        const Triangle& tri = mesh.triangles[math::id_from_key(key)];
        math::Vec4f clip[3];
        math::Vec3f normal[3];
        math::Vec3f albedo[3];
        for (int i = 0; i < 3; ++i) {
          const std::uint32_t v = tri.v[i];
          clip[i] = sv[v].clip;
          normal[i] = normals[v];
          albedo[i] = mesh.has_colors() ? math::vec3(mesh.colors[v].x, mesh.colors[v].y, mesh.colors[v].z)
                                        : k.albedo;
        }
        rgba[p] = pipeline::shade_sample(k, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), clip,
                                         normal, albedo);
      }
    }
    counts.covered += covered;
  });
  stats.shade_ms = ms_since(start);

  stats.triangles_rasterized = counts.rasterized;
  stats.triangles_clipped = counts.clipped;
  stats.pixels_covered = counts.covered;
  stats.total_ms = ms_since(frame_start);
}

}  // namespace rasty
