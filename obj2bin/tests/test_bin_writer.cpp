// Writing .bin files: the layout, determinism, and the checks a mesh must pass first.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <random>
#include <string>

#include "rasty/bin_format.hpp"
#include "test_util.hpp"

namespace rasty {
namespace {

using test::expect_bin_holds;
using test::expect_error;
using test::read_bytes;
using test::TempDir;
using test::tetrahedron;

template <typename T>
T field_at(const std::string& bytes, std::size_t offset) {
  T v;
  std::memcpy(&v, bytes.data() + offset, sizeof v);
  return v;
}

TEST(BinWriter, WritesEveryValue) {
  TempDir dir;
  for (const bool colors : {false, true}) {
    const Mesh m = tetrahedron(colors);
    const std::string path = dir.file(colors ? "c.bin" : "n.bin");
    const Bounds written = save_mesh(path, m);
    expect_bin_holds(path, m);
    const Bounds expected = compute_bounds(m.positions);
    EXPECT_TRUE(test::same_bits(written.min, expected.min));
    EXPECT_TRUE(test::same_bits(written.center, expected.center));
    EXPECT_EQ(written.radius, expected.radius);
  }
}

TEST(BinWriter, LayoutMatchesTheSpec) {
  TempDir dir;
  struct Case {
    bool colors;
    std::uint64_t normals, colors_at, indices, size;
  };
  // V = 4, T = 4: each array is 4 records of 16 bytes, so each one ends on a multiple of 64.
  for (const Case c : {Case{false, 192, 0, 256, 320}, Case{true, 192, 256, 320, 384}}) {
    const Mesh m = tetrahedron(c.colors);
    const std::string path = dir.file("m.bin");
    save_mesh(path, m);
    const std::string b = read_bytes(path);
    ASSERT_EQ(b.size(), c.size);
    EXPECT_EQ(b.substr(0, 8), "RASTYBIN");
    EXPECT_EQ(field_at<std::uint32_t>(b, 8), 2u);    // version
    EXPECT_EQ(field_at<std::uint32_t>(b, 12), 128u); // header size
    EXPECT_EQ(field_at<std::uint32_t>(b, 16), c.colors ? 1u : 0u);
    EXPECT_EQ(field_at<std::uint32_t>(b, 20), 4u);   // vertices
    EXPECT_EQ(field_at<std::uint32_t>(b, 24), 4u);   // triangles
    EXPECT_EQ(field_at<std::uint32_t>(b, 28), 0u);
    EXPECT_EQ(field_at<std::uint64_t>(b, 32), 128u);
    EXPECT_EQ(field_at<std::uint64_t>(b, 40), c.normals);
    EXPECT_EQ(field_at<std::uint64_t>(b, 48), c.colors_at);
    EXPECT_EQ(field_at<std::uint64_t>(b, 56), c.indices);
    EXPECT_EQ(field_at<std::uint64_t>(b, 64), c.size);
    const Bounds bounds = compute_bounds(m.positions);
    EXPECT_TRUE(test::same_bits(field_at<Vec3f>(b, 72), bounds.min));
    EXPECT_TRUE(test::same_bits(field_at<Vec3f>(b, 84), bounds.max));
    EXPECT_TRUE(test::same_bits(field_at<Vec3f>(b, 96), bounds.center));
    EXPECT_EQ(field_at<float>(b, 108), bounds.radius);
    for (std::size_t i = 112; i < 128; ++i) EXPECT_EQ(b[i], 0) << "reserved byte " << i;

    // Every element is a 16-byte record: x, y, z and w = 1 for positions and colors, w = 0 for normals;
    // three indices and a 0 for triangles.
    const auto record = [&](std::uint64_t at) {
      return std::array<float, 4>{field_at<float>(b, at), field_at<float>(b, at + 4), field_at<float>(b, at + 8),
                                  field_at<float>(b, at + 12)};
    };
    EXPECT_EQ(record(128 + 16), (std::array<float, 4>{1, -1, -1, 1}));  // position 1
    const Vec3f n2 = m.normals[2];
    EXPECT_EQ(record(c.normals + 32), (std::array<float, 4>{n2.x, n2.y, n2.z, 0}));
    if (c.colors) EXPECT_EQ(record(c.colors_at + 48), (std::array<float, 4>{0.5f, 0.5f, 0.5f, 1}));  // color 3
    for (std::uint64_t k = 0; k < 4; ++k) {  // triangle 1 = (0, 3, 1)
      EXPECT_EQ(field_at<std::uint32_t>(b, c.indices + 16 + 4 * k), (std::array<std::uint32_t, 4>{0, 3, 1, 0}[k]));
    }
    expect_bin_holds(path, m);
  }
}

TEST(BinWriter, ArraysStartOnCacheLines) {
  // V = 5: 80 bytes of positions, so the normals start after 48 bytes of zero padding, at 256.
  Mesh m;
  m.positions = {{0, 1, 0}, {1, 0, 1}, {1, 0, -1}, {-1, 0, -1}, {-1, 0, 1}};
  for (const Vec3f& p : m.positions) m.normals.push_back(test::unit(p.x, p.y - 0.5f, p.z));
  m.triangles = {{{0, 1, 2}}, {{0, 2, 3}}, {{0, 3, 4}}, {{0, 4, 1}}, {{1, 4, 3}}, {{1, 3, 2}}};
  TempDir dir;
  save_mesh(dir.file("p.bin"), m);
  const std::string b = read_bytes(dir.file("p.bin"));
  EXPECT_EQ(field_at<std::uint64_t>(b, 40), 256u);  // normals: align64(128 + 80)
  EXPECT_EQ(field_at<std::uint64_t>(b, 56), 384u);  // indices: align64(256 + 80)
  EXPECT_EQ(b.size(), 384u + 6 * 16);
  expect_bin_holds(dir.file("p.bin"), m);  // includes the zero padding between the arrays
}

TEST(BinWriter, SameMeshGivesTheSameBytes) {
  TempDir dir;
  save_mesh(dir.file("a.bin"), tetrahedron(true));
  save_mesh(dir.file("b.bin"), tetrahedron(true));
  EXPECT_EQ(read_bytes(dir.file("a.bin")), read_bytes(dir.file("b.bin")));
}

TEST(BinWriter, ReplacesAnExistingFile) {
  TempDir dir;
  const std::string path = dir.file("m.bin");
  save_mesh(path, tetrahedron(true));
  save_mesh(path, tetrahedron(false));
  expect_bin_holds(path, tetrahedron(false));
  EXPECT_FALSE(std::filesystem::exists(path + ".tmp"));
}

TEST(BinWriter, BoundsEncloseEveryVertex) {
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> coord(-50.0f, 120.0f);
  std::vector<Vec3f> pts(2000);
  for (Vec3f& p : pts) p = Vec3f{coord(rng), coord(rng), coord(rng) * 0.01f};
  const Bounds b = compute_bounds(pts);
  double max_dist = 0.0;
  for (const Vec3f& p : pts) {
    EXPECT_LE(b.min.x, p.x);
    EXPECT_LE(b.min.y, p.y);
    EXPECT_LE(b.min.z, p.z);
    EXPECT_GE(b.max.x, p.x);
    EXPECT_GE(b.max.y, p.y);
    EXPECT_GE(b.max.z, p.z);
    const double dx = static_cast<double>(p.x) - b.center.x, dy = static_cast<double>(p.y) - b.center.y,
                 dz = static_cast<double>(p.z) - b.center.z;
    max_dist = std::max(max_dist, std::sqrt(dx * dx + dy * dy + dz * dz));
  }
  EXPECT_GE(static_cast<double>(b.radius), max_dist);
  EXPECT_LE(static_cast<double>(b.radius), max_dist * (1 + 1e-6));
  EXPECT_FLOAT_EQ(b.center.x, (b.min.x + b.max.x) / 2);
}

TEST(BinWriter, RejectsMeshesThatBreakTheRules) {
  TempDir dir;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  struct Case {
    const char* what;
    std::function<void(Mesh&)> change;
    const char* message;
  };
  const Case cases[] = {
      {"no triangles", [](Mesh& m) { m.triangles.clear(); }, "no triangles"},
      {"no vertices", [](Mesh& m) { m = Mesh{}; }, "no vertices"},
      {"normal count", [](Mesh& m) { m.normals.pop_back(); }, "4 vertices but 3 normals"},
      {"color count", [](Mesh& m) { m.colors.pop_back(); }, "4 vertices but 3 colors"},
      {"index", [](Mesh& m) { m.triangles[2].v[1] = 9; }, "triangle 2 uses vertex 9"},
      {"long normal", [](Mesh& m) { m.normals[3].x *= 2; }, "normal 3"},
      {"zero normal", [](Mesh& m) { m.normals[0] = Vec3f{0, 0, 0}; }, "normal 0"},
      {"nan position", [nan](Mesh& m) { m.positions[2].y = nan; }, "position 2 is not finite"},
      {"color range", [](Mesh& m) { m.colors[1].z = 1.5f; }, "color 1"},
      {"negative color", [](Mesh& m) { m.colors[1].z = -0.0001f; }, "color 1"},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(c.what);
    Mesh m = tetrahedron(true);
    c.change(m);
    const std::string path = dir.file("m.bin");
    expect_error<MeshError>([&] { save_mesh(path, m); }, c.message);
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_FALSE(std::filesystem::exists(path + ".tmp"));
  }
}

}  // namespace
}  // namespace rasty
