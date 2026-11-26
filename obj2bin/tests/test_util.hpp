// Helpers shared by the tests.
#pragma once

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

#include "rasty/bin_format.hpp"

namespace rasty::test {

// A fresh directory for one test, removed afterwards.
class TempDir {
 public:
  TempDir() {
    const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
    path_ = std::filesystem::temp_directory_path() /
            ("rasty_" + std::string(info->test_suite_name()) + "_" + info->name() + "_" + std::to_string(::getpid()));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  std::string file(const std::string& name) const { return (path_ / name).string(); }
  std::string dir() const { return path_.string() + "/"; }

 private:
  std::filesystem::path path_;
};

inline void write_file(const std::string& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  ASSERT_TRUE(out.good()) << path;
}

inline std::string read_bytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline bool same_bits(const Vec3f& a, const Vec3f& b) { return std::memcmp(&a, &b, sizeof(Vec3f)) == 0; }

inline Vec3f unit(float x, float y, float z) {
  const float len = std::sqrt(x * x + y * y + z * z);
  return Vec3f{x / len, y / len, z / len};
}

// A regular tetrahedron-like mesh: 4 vertices, 4 outward-facing triangles, normals pointing away from the
// center.
inline Mesh tetrahedron(bool with_colors = false) {
  Mesh m;
  m.positions = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};
  for (const Vec3f& p : m.positions) m.normals.push_back(unit(p.x, p.y, p.z));
  m.triangles = {{{0, 1, 2}}, {{0, 3, 1}}, {{0, 2, 3}}, {{1, 3, 2}}};
  if (with_colors) m.colors = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.5f, 0.5f, 0.5f}};
  return m;
}

inline void expect_same_mesh(const Mesh& a, const Mesh& b) {
  ASSERT_EQ(a.positions.size(), b.positions.size());
  ASSERT_EQ(a.normals.size(), b.normals.size());
  ASSERT_EQ(a.colors.size(), b.colors.size());
  ASSERT_EQ(a.triangles.size(), b.triangles.size());
  for (std::size_t i = 0; i < a.positions.size(); ++i) {
    EXPECT_TRUE(same_bits(a.positions[i], b.positions[i])) << "position " << i;
    EXPECT_TRUE(same_bits(a.normals[i], b.normals[i])) << "normal " << i;
  }
  for (std::size_t i = 0; i < a.colors.size(); ++i) EXPECT_TRUE(same_bits(a.colors[i], b.colors[i])) << "color " << i;
  for (std::size_t t = 0; t < a.triangles.size(); ++t) {
    for (int k = 0; k < 3; ++k) EXPECT_EQ(a.triangles[t].v[k], b.triangles[t].v[k]) << "triangle " << t;
  }
}

// Checks byte by byte that the file at `path` holds `mesh` in the documented layout: header fields,
// bounds, each array at its offset as 16-byte records with the right fourth component, and zeros
// everywhere else.
inline void expect_bin_holds(const std::string& path, const Mesh& mesh) {
  const std::string b = read_bytes(path);
  ASSERT_GE(b.size(), sizeof(bin::Header));
  bin::Header h;
  std::memcpy(&h, b.data(), sizeof h);

  const auto align = [](std::uint64_t n) { return (n + 63) / 64 * 64; };
  const std::uint64_t nv = mesh.vertex_count(), nt = mesh.triangle_count();
  const std::uint64_t normals = align(128 + 16 * nv);
  std::uint64_t next = align(normals + 16 * nv);
  const std::uint64_t colors = mesh.has_colors() ? next : 0;
  if (mesh.has_colors()) next = align(colors + 16 * nv);
  const std::uint64_t indices = next, size = indices + 16 * nt;

  EXPECT_EQ(std::string(h.magic, 8), "RASTYBIN");
  EXPECT_EQ(h.version, 2u);
  EXPECT_EQ(h.header_size, 128u);
  EXPECT_EQ(h.flags, mesh.has_colors() ? 1u : 0u);
  EXPECT_EQ(h.vertex_count, nv);
  EXPECT_EQ(h.triangle_count, nt);
  EXPECT_EQ(h.positions_offset, 128u);
  EXPECT_EQ(h.normals_offset, normals);
  EXPECT_EQ(h.colors_offset, colors);
  EXPECT_EQ(h.indices_offset, indices);
  EXPECT_EQ(h.file_size, size);
  ASSERT_EQ(b.size(), size);
  const Bounds bounds = compute_bounds(mesh.positions);
  EXPECT_TRUE(same_bits(h.bounds_min, bounds.min));
  EXPECT_TRUE(same_bits(h.bounds_max, bounds.max));
  EXPECT_TRUE(same_bits(h.center, bounds.center));
  EXPECT_EQ(h.radius, bounds.radius);

  std::string rest = b;  // everything but the header fields and the records must be zero
  std::memset(&rest[0], 0, offsetof(bin::Header, reserved1));
  const auto take = [&](std::uint64_t at, const void* expected, std::size_t bytes, const char* what, std::size_t i) {
    EXPECT_EQ(std::memcmp(b.data() + at, expected, bytes), 0) << what << " " << i;
    std::memset(&rest[at], 0, bytes);
  };
  const auto vertices = [&](std::uint64_t offset, const std::vector<Vec3f>& values, float w, const char* what) {
    for (std::size_t i = 0; i < values.size(); ++i) {
      const float record[4] = {values[i].x, values[i].y, values[i].z, w};
      take(offset + 16 * i, record, 16, what, i);
    }
  };
  vertices(128, mesh.positions, 1.0f, "position");
  vertices(normals, mesh.normals, 0.0f, "normal");
  if (mesh.has_colors()) vertices(colors, mesh.colors, 1.0f, "color");
  for (std::size_t t = 0; t < nt; ++t) {
    const std::uint32_t record[4] = {mesh.triangles[t].v[0], mesh.triangles[t].v[1], mesh.triangles[t].v[2], 0};
    take(indices + 16 * t, record, 16, "triangle", t);
  }
  EXPECT_EQ(rest, std::string(rest.size(), '\0')) << "padding and reserved bytes must be zero";
}

// Asserts that `fn` throws E with a message containing `part`.
template <typename E, typename Fn>
void expect_error(Fn&& fn, const std::string& part) {
  try {
    fn();
    ADD_FAILURE() << "no exception; expected one containing '" << part << "'";
  } catch (const E& e) {
    EXPECT_NE(std::string(e.what()).find(part), std::string::npos) << "message: " << e.what() << "\nexpected: " << part;
  }
}

}  // namespace rasty::test
