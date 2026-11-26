// Tests for the shared pipeline stages (pipeline.h) and the CPU renderer that loops over them.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "cpu_renderer.h"
#include "orbit_camera.h"
#include "pipeline.h"

namespace rasty {
namespace {

using math::IVec2;
using pipeline::FrameConstants;
using pipeline::TriSetup;

// A fixed-point position on the center of pixel (x, y): every such point lies exactly on some edges.
IVec2 at_pixel_center(int x, int y) { return math::pixel_center(x, y); }

// Back faces are kept, so the coverage tests see both windings (setup swaps corners for one of them).
FrameConstants constants(int width, int height) {
  FrameConstants k =
      pipeline::make_frame_constants(math::identity(), math::identity(), math::identity(), width, height);
  k.cull_back_faces = false;
  return k;
}

// Small deterministic generator, so the "random" cases are the same on every machine.
struct Lcg {
  std::uint32_t state;
  std::uint32_t next() { return state = state * 1664525u + 1013904223u; }
  int in(int lo, int hi) { return lo + static_cast<int>(next() >> 8) % (hi - lo + 1); }
};

// --- pipeline.h -------------------------------------------------------------------------------------

TEST(Pipeline, TrianglesSharingAnEdgeCoverEachPixelOnce) {
  const FrameConstants k = constants(16, 16);
  // A 6x6-pixel square from pixel center (2,2) to (8,8), split along its diagonal, in both windings.
  const IVec2 a = at_pixel_center(2, 2), b = at_pixel_center(8, 2), c = at_pixel_center(8, 8),
              d = at_pixel_center(2, 8);
  for (const bool flip : {false, true}) {
  TriSetup t0, t1;
  ASSERT_TRUE(flip ? pipeline::setup_triangle(k, a, c, b, 0.5f, 0.5f, 0.5f, 0, t0)
                   : pipeline::setup_triangle(k, a, b, c, 0.5f, 0.5f, 0.5f, 0, t0));
  ASSERT_TRUE(flip ? pipeline::setup_triangle(k, a, d, c, 0.5f, 0.5f, 0.5f, 1, t1)
                   : pipeline::setup_triangle(k, a, c, d, 0.5f, 0.5f, 0.5f, 1, t1));
  int covered = 0;
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      const int hits = (pipeline::cover_pixel(t0, x, y) != math::kNoDepthId) +
                       (pipeline::cover_pixel(t1, x, y) != math::kNoDepthId);
      EXPECT_LE(hits, 1) << "pixel " << x << "," << y << " is covered by both triangles";
      // The top-left rule makes the square half-open: columns and rows 2..7 belong to it, 8 does not.
      const bool inside = x >= 2 && x < 8 && y >= 2 && y < 8;
      EXPECT_EQ(hits, inside ? 1 : 0) << "pixel " << x << "," << y;
      covered += hits;
    }
  }
  EXPECT_EQ(covered, 36) << (flip ? "flipped winding" : "original winding");
  }
}

TEST(Pipeline, AJitteredGridHasNoGapsOrOverlaps) {
  // 8x6 grid cells of 6 pixels, each corner moved by up to 2 pixels in subpixel steps; two triangles per
  // cell. Pixels near the grid's middle must be covered exactly once, and no pixel more than once.
  const int cols = 8, rows = 6, cell = 6, width = cols * cell + 8, height = rows * cell + 8;
  const FrameConstants k = constants(width, height);
  Lcg rng{12345};
  std::vector<IVec2> grid;
  for (int j = 0; j <= rows; ++j) {
    for (int i = 0; i <= cols; ++i) {
      grid.push_back({(4 + i * cell) * 256 + rng.in(-512, 512), (4 + j * cell) * 256 + rng.in(-512, 512)});
    }
  }
  std::vector<int> hits(static_cast<std::size_t>(width * height), 0);
  auto draw = [&](IVec2 p, IVec2 q, IVec2 r) {
    TriSetup t;
    if (!pipeline::setup_triangle(k, p, q, r, 0.5f, 0.5f, 0.5f, 0, t)) return;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) hits[static_cast<std::size_t>(y * width + x)] += pipeline::cover_pixel(t, x, y) != math::kNoDepthId;
  };
  for (int j = 0; j < rows; ++j) {
    for (int i = 0; i < cols; ++i) {
      const IVec2 p00 = grid[static_cast<std::size_t>(j * (cols + 1) + i)], p10 = grid[static_cast<std::size_t>(j * (cols + 1) + i + 1)];
      const IVec2 p01 = grid[static_cast<std::size_t>((j + 1) * (cols + 1) + i)], p11 = grid[static_cast<std::size_t>((j + 1) * (cols + 1) + i + 1)];
      draw(p00, p10, p11);
      draw(p00, p11, p01);
    }
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int h = hits[static_cast<std::size_t>(y * width + x)];
      EXPECT_LE(h, 1) << "overlap at " << x << "," << y;
      const bool interior = x >= 7 && x < 4 + cols * cell - 3 && y >= 7 && y < 4 + rows * cell - 3;
      if (interior) EXPECT_EQ(h, 1) << "gap at " << x << "," << y;
    }
  }
}

TEST(Pipeline, SteppingMatchesDirectEvaluation) {
  // raster_triangle steps edge values pixel by pixel; cover_pixel computes them from scratch. They must
  // agree on every pixel, key included, or the CPU and a per-pixel GPU kernel would differ.
  const int width = 64, height = 48;
  const FrameConstants k = constants(width, height);
  Lcg rng{777};
  int drawn = 0;
  for (int n = 0; n < 300; ++n) {
    const IVec2 a{rng.in(-2000, 17000), rng.in(-2000, 13000)};
    const IVec2 b{rng.in(-2000, 17000), rng.in(-2000, 13000)};
    const IVec2 c{rng.in(-2000, 17000), rng.in(-2000, 13000)};
    TriSetup t;
    if (!pipeline::setup_triangle(k, a, b, c, 0.1f, 0.6f, 0.9f, static_cast<std::uint32_t>(n), t)) continue;
    ++drawn;
    std::vector<std::uint64_t> stepped(static_cast<std::size_t>(width * height), math::kNoDepthId);
    pipeline::raster_triangle(t, width, [&](std::int64_t pixel, std::uint64_t key) { stepped[static_cast<std::size_t>(pixel)] = key; });
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        ASSERT_EQ(stepped[static_cast<std::size_t>(y * width + x)], pipeline::cover_pixel(t, x, y)) << "triangle " << n << " pixel " << x << "," << y;
  }
  EXPECT_GT(drawn, 250);  // all but the few with no pixel center inside
}

TEST(Pipeline, ClippingKeepsOnlyThePartInFrontOfTheNearPlane) {
  const FrameConstants k = constants(100, 100);
  // Clip space with identity projection: z >= 0 is in front of the near plane.
  const pipeline::ClipPolygon p = pipeline::clip_triangle(k, math::vec4(-0.5f, -0.5f, 0.5f, 1.0f),
                                                          math::vec4(0.5f, -0.5f, -0.5f, 1.0f),
                                                          math::vec4(0.0f, 0.5f, 0.5f, 1.0f));
  ASSERT_EQ(p.count, 4);  // one corner cut off: a quad
  for (int i = 0; i < p.count; ++i) EXPECT_GE(p.v[i].z, 0.0f);
}

// --- CpuRenderer ------------------------------------------------------------------------------------

// A mesh from positions and triangles; every normal points along +Z unless given.
Mesh make_mesh(std::vector<Vec3f> positions, std::vector<Triangle> triangles, std::vector<Vec3f> colors = {},
               Vec3f normal = {0.0f, 0.0f, 1.0f}) {
  Mesh m;
  m.positions = std::move(positions);
  m.normals.assign(m.positions.size(), normal);
  m.colors = std::move(colors);
  m.triangles = std::move(triangles);
  return m;
}

// Camera at the origin looking down -Z, 90-degree view; ambient 1 and diffuse 0 so pixels show the
// albedo unchanged.
RenderSettings flat_settings(std::uint32_t size) {
  RenderSettings s;
  s.width = size;
  s.height = size;
  s.projection = math::perspective(1.5707964f, 1.0f, 0.1f, 10.0f);
  s.ambient = 1.0f;
  s.diffuse = 0.0f;
  return s;
}

std::uint32_t rgb(float r, float g, float b) { return math::to_rgba8(math::vec3(r, g, b)); }

// A square at depth z (counter-clockwise seen from the camera) in one color.
void add_square(Mesh& m, float half, float z, Vec3f color) {
  const auto base = static_cast<std::uint32_t>(m.positions.size());
  for (const Vec3f p : {Vec3f{-half, -half, z}, Vec3f{half, -half, z}, Vec3f{half, half, z}, Vec3f{-half, half, z}}) {
    m.positions.push_back(p);
    m.normals.push_back({0.0f, 0.0f, 1.0f});
    m.colors.push_back(color);
  }
  m.triangles.push_back({{base, base + 1, base + 2}});
  m.triangles.push_back({{base, base + 2, base + 3}});
}

TEST(CpuRenderer, NearerSurfaceWinsWhicheverIsDrawnFirst) {
  const CpuRenderer renderer(CpuRendererOptions{1});
  for (const bool red_first : {true, false}) {
    Mesh m;
    if (red_first) add_square(m, 1.0f, -3.0f, {1, 0, 0});
    add_square(m, 0.5f, -2.0f, {0, 1, 0});  // nearer, smaller
    if (!red_first) add_square(m, 1.0f, -3.0f, {1, 0, 0});
    const FrameBuffer f = renderer.render(m, flat_settings(64));
    ASSERT_TRUE(f.valid());
    EXPECT_EQ(f.rgba[32 * 64 + 32], rgb(0, 1, 0));  // center: the near green square
    EXPECT_EQ(f.rgba[32 * 64 + 22], rgb(1, 0, 0));  // left of center: only the far red square
    EXPECT_EQ(f.rgba[0], rgb(0, 0, 0));             // corner: background
  }
}

TEST(CpuRenderer, EqualDepthsGoToTheLowerTriangleId) {
  const CpuRenderer renderer(CpuRendererOptions{1});
  Mesh first_red, first_blue;
  add_square(first_red, 0.6f, -2.0f, {1, 0, 0});
  add_square(first_red, 0.6f, -2.0f, {0, 0, 1});
  add_square(first_blue, 0.6f, -2.0f, {0, 0, 1});
  add_square(first_blue, 0.6f, -2.0f, {1, 0, 0});
  EXPECT_EQ(renderer.render(first_red, flat_settings(32)).rgba[16 * 32 + 16], rgb(1, 0, 0));
  EXPECT_EQ(renderer.render(first_blue, flat_settings(32)).rgba[16 * 32 + 16], rgb(0, 0, 1));
}

TEST(CpuRenderer, BackFacesAreCulledUnlessAskedFor) {
  // Clockwise as seen from the camera: a back face.
  const Mesh m = make_mesh({{-1, -1, -2}, {0, 1, -2}, {1, -1, -2}}, {{{0, 1, 2}}});
  CpuRendererOptions cull;
  cull.threads = 1;
  EXPECT_EQ(CpuRenderer(cull).render(m, flat_settings(32)).rgba[16 * 32 + 16], rgb(0, 0, 0));
  CpuRendererOptions keep = cull;
  keep.cull_back_faces = false;
  EXPECT_NE(CpuRenderer(keep).render(m, flat_settings(32)).rgba[16 * 32 + 16], rgb(0, 0, 0));
}

TEST(CpuRenderer, ClipsAGroundPlaneThatPassesBehindTheCamera) {
  // A floor one unit below the eye, reaching 50 units in front of and behind the camera.
  const Mesh m = make_mesh({{-50, -1, 50}, {50, -1, 50}, {50, -1, -50}, {-50, -1, -50}},
                           {{{0, 1, 2}}, {{0, 2, 3}}}, {}, {0.0f, 1.0f, 0.0f});
  const CpuRenderer renderer(CpuRendererOptions{1});
  const std::uint32_t size = 64;
  const FrameBuffer f = renderer.render(m, flat_settings(size));
  EXPECT_EQ(renderer.last_stats().triangles_clipped, 2u);
  for (std::uint32_t x = 0; x < size; ++x) {
    EXPECT_EQ(f.rgba[x], rgb(0, 0, 0)) << "top row, x " << x;                         // sky
    EXPECT_NE(f.rgba[(size - 1) * size + x], rgb(0, 0, 0)) << "bottom row, x " << x;  // floor
  }
}

TEST(CpuRenderer, AHugeTriangleIsClippedToTheGuardBandAndCoversEverything) {
  const Mesh m = make_mesh({{-1e5f, -1e5f, -2}, {1e5f, -1e5f, -2}, {0, 1e5f, -2}}, {{{0, 1, 2}}});
  const CpuRenderer renderer(CpuRendererOptions{1});
  const FrameBuffer f = renderer.render(m, flat_settings(48));
  EXPECT_EQ(renderer.last_stats().triangles_clipped, 1u);
  EXPECT_EQ(renderer.last_stats().pixels_covered, 48u * 48u);
}

// A closed, bumpy sphere with a few thousand triangles, for whole-frame comparisons.
Mesh bumpy_sphere(int stacks, int slices) {
  Mesh m;
  for (int i = 0; i <= stacks; ++i) {
    const float v = static_cast<float>(i) / static_cast<float>(stacks) * 3.14159265f;
    for (int j = 0; j <= slices; ++j) {
      const float u = static_cast<float>(j) / static_cast<float>(slices) * 6.28318531f;
      const float r = 1.0f + 0.08f * std::sin(5.0f * u) * std::sin(4.0f * v);
      const Vec3f n{std::sin(v) * std::cos(u), std::cos(v), std::sin(v) * std::sin(u)};
      m.positions.push_back({r * n.x, r * n.y, r * n.z});
      m.normals.push_back(n);
    }
  }
  const auto row = static_cast<std::uint32_t>(slices + 1);
  for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(stacks); ++i) {
    for (std::uint32_t j = 0; j < static_cast<std::uint32_t>(slices); ++j) {
      const std::uint32_t a = i * row + j, b = a + 1, c = a + row, d = c + 1;
      m.triangles.push_back({{a, b, d}});
      m.triangles.push_back({{a, d, c}});
    }
  }
  return m;
}

TEST(CpuRenderer, SameImageForAnyThreadCount) {
  const Mesh m = bumpy_sphere(60, 90);
  const RenderSettings s = orbit_settings(compute_bounds(m.positions), 320, 240, OrbitView{});
  const FrameBuffer one = CpuRenderer(CpuRendererOptions{1}).render(m, s);
  for (const unsigned threads : {2u, 3u, 8u}) {
    const FrameBuffer many = CpuRenderer(CpuRendererOptions{threads}).render(m, s);
    EXPECT_EQ(many.rgba, one.rgba) << threads << " threads";
  }
}

TEST(CpuRenderer, ReusesItsBuffersAcrossFramesAndSizes) {
  const Mesh m = bumpy_sphere(20, 30);
  const CpuRenderer renderer(CpuRendererOptions{4});
  FrameBuffer frame;
  for (const std::uint32_t w : {200u, 64u, 300u}) {
    const RenderSettings s = orbit_settings(compute_bounds(m.positions), w, w / 2, OrbitView{});
    renderer.render_into(m, s, frame);
    ASSERT_TRUE(frame.valid());
    EXPECT_EQ(frame.rgba, renderer.render(m, s).rgba);
    EXPECT_GT(renderer.last_stats().pixels_covered, 0u);
  }
}

}  // namespace
}  // namespace rasty
