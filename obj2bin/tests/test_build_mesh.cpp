// build_mesh() and convert_model(): the steps that make a model ready to render, and the guarantees the
// converter's output keeps.
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "convert.hpp"
#include "test_util.hpp"

namespace rasty::convert {
namespace {

using test::expect_error;
using test::expect_same_mesh;
using test::same_bits;
using test::TempDir;
using test::write_file;

using Tri = std::array<std::uint32_t, 3>;

SourceMesh source(std::vector<Vec3f> positions, const std::vector<Tri>& tris) {
  SourceMesh s;
  s.positions = std::move(positions);
  for (const Tri& t : tris) {
    for (const std::uint32_t p : t) {
      Corner c;
      c.position = p;
      s.corners.push_back(c);
    }
  }
  return s;
}

const std::vector<Vec3f> kSquare = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};

std::vector<Tri> triangles_of(const Mesh& m) {
  std::vector<Tri> out;
  for (const Triangle& t : m.triangles) out.push_back({t.v[0], t.v[1], t.v[2]});
  return out;
}

// Gives the corners of triangle t the normals (or colors) n0, n1, n2.
void set_normals(SourceMesh& s, std::size_t t, Tri n) {
  for (std::size_t k = 0; k < 3; ++k) s.corners[3 * t + k].normal = n[k];
}
void set_colors(SourceMesh& s, std::size_t t, Tri c) {
  for (std::size_t k = 0; k < 3; ++k) s.corners[3 * t + k].color = c[k];
}

TEST(BuildMesh, SharedVerticesStayShared) {
  BuildStats st;
  const Mesh m = build_mesh(source(kSquare, {{0, 1, 2}, {0, 2, 3}}), {}, &st);
  EXPECT_EQ(m.vertex_count(), 4u);
  EXPECT_EQ(triangles_of(m), (std::vector<Tri>{{0, 1, 2}, {0, 2, 3}}));
  for (const Vec3f& n : m.normals) EXPECT_TRUE(same_bits(n, Vec3f{0, 0, 1}));
  EXPECT_FALSE(m.has_colors());
  EXPECT_EQ(st.computed_normals, 4u);
  EXPECT_EQ(st.split_vertices + st.unused_positions + st.degenerate_triangles + st.file_normals, 0u);
  EXPECT_NO_THROW(validate_mesh(m));
}

TEST(BuildMesh, ComputedNormalsAreAngleWeightedAndFollowTheWinding) {
  // Triangle A lies in the xy plane with area 2 (normal +z); triangle B in the xz plane with area 1/2,
  // wound so that its normal is +y. Vertex 0 has a right angle in both, so both count equally, whatever
  // their areas.
  const std::vector<Vec3f> p = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {0, 0, 1}, {1, 0, 0}};
  const Mesh m = build_mesh(source(p, {{0, 1, 2}, {0, 3, 4}}), {});
  const float h = 1.0f / std::sqrt(2.0f);
  EXPECT_NEAR(m.normals[0].x, 0.0f, 1e-7);
  EXPECT_NEAR(m.normals[0].y, h, 1e-7);
  EXPECT_NEAR(m.normals[0].z, h, 1e-7);
  EXPECT_TRUE(same_bits(m.normals[1], Vec3f{0, 0, 1}));
  EXPECT_TRUE(same_bits(m.normals[3], Vec3f{0, 1, 0}));

  const Mesh flipped = build_mesh(source(p, {{0, 2, 1}}), {});  // clockwise seen from +z
  for (const Vec3f& n : flipped.normals) EXPECT_TRUE(same_bits(n, Vec3f{0, 0, -1}));
}

TEST(BuildMesh, QuadNormalsDoNotDependOnTheDiagonal) {
  // A cube of quads, split along one diagonal of every face and then along the other. Area weights would
  // tilt each corner's normal towards the faces that put two triangles at that corner.
  const std::vector<Vec3f> p = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                                {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
  const std::uint32_t quads[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
  const float k = 1.0f / std::sqrt(3.0f);
  for (const std::uint32_t rot : {0u, 1u}) {
    SCOPED_TRACE(rot);
    std::vector<Tri> tris;
    for (const auto& q : quads) {
      const std::uint32_t a = q[rot], b = q[(rot + 1) % 4], c = q[(rot + 2) % 4], d = q[(rot + 3) % 4];
      tris.push_back({a, b, c});
      tris.push_back({a, c, d});
    }
    const Mesh m = build_mesh(source(p, tris), {});
    ASSERT_EQ(m.vertex_count(), 8u);
    for (std::size_t v = 0; v < 8; ++v) {  // every corner's normal points straight out along the diagonal
      EXPECT_NEAR(m.normals[v].x, m.positions[v].x * k, 1e-7) << "vertex " << v;
      EXPECT_NEAR(m.normals[v].y, m.positions[v].y * k, 1e-7) << "vertex " << v;
      EXPECT_NEAR(m.normals[v].z, m.positions[v].z * k, 1e-7) << "vertex " << v;
    }
  }
}

TEST(BuildMesh, DegenerateTrianglesAreRemoved) {
  // Vertex 3 is a second vertex at vertex 1's position.
  const std::vector<Vec3f> p = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 0, 0}};
  BuildStats st;
  const Mesh m = build_mesh(source(p, {{0, 1, 2}, {0, 0, 1}, {1, 3, 2}, {0, 3, 2}}), {}, &st);
  EXPECT_EQ(st.degenerate_triangles, 2u);
  EXPECT_EQ(triangles_of(m), (std::vector<Tri>{{0, 1, 2}, {0, 3, 2}}));
  EXPECT_EQ(m.vertex_count(), 4u);  // the file's vertices 1 and 3 stay separate vertices

  expect_error<ConvertError>([&] { build_mesh(source(p, {{1, 3, 2}}), {}); }, "no triangles left: all 1");
}

TEST(BuildMesh, VerticesAreNumberedInOrderOfFirstUse) {
  const std::vector<Vec3f> p = {{9, 9, 9}, {0, 0, 0}, {8, 8, 8}, {1, 0, 0}, {1, 1, 0}, {0, -1, 0}};
  BuildStats st;
  const Mesh m = build_mesh(source(p, {{5, 3, 1}, {1, 3, 4}}), {}, &st);
  EXPECT_EQ(triangles_of(m), (std::vector<Tri>{{0, 1, 2}, {2, 1, 3}}));
  ASSERT_EQ(m.vertex_count(), 4u);
  EXPECT_TRUE(same_bits(m.positions[0], p[5]));
  EXPECT_TRUE(same_bits(m.positions[1], p[3]));
  EXPECT_TRUE(same_bits(m.positions[2], p[1]));
  EXPECT_TRUE(same_bits(m.positions[3], p[4]));
  EXPECT_EQ(st.unused_positions, 2u);
  const Bounds b = compute_bounds(m.positions);
  EXPECT_TRUE(same_bits(b.min, Vec3f{0, -1, 0}));  // unused vertices are not in the mesh
  EXPECT_TRUE(same_bits(b.max, Vec3f{1, 1, 0}));
}

TEST(BuildMesh, FileNormalsSplitAVertexOnlyWhenTheyDiffer) {
  SourceMesh s = source(kSquare, {{0, 1, 2}, {0, 2, 3}, {0, 2, 3}});
  s.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 3}, {2, 0, 0}};
  set_normals(s, 0, {0, 0, 0});
  set_normals(s, 1, {1, 2, 3});  // same values as normal 0 once normalized, except for normal 3
  set_normals(s, 2, {3, 0, 3});  // vertex 0 with a different normal: a second vertex
  BuildStats st;
  const Mesh m = build_mesh(s, {}, &st);
  EXPECT_EQ(triangles_of(m), (std::vector<Tri>{{0, 1, 2}, {0, 2, 3}, {4, 2, 3}}));
  ASSERT_EQ(m.vertex_count(), 5u);
  EXPECT_TRUE(same_bits(m.normals[2], Vec3f{0, 0, 1}));
  EXPECT_TRUE(same_bits(m.normals[3], Vec3f{1, 0, 0}));
  EXPECT_TRUE(same_bits(m.normals[4], Vec3f{1, 0, 0}));
  EXPECT_TRUE(same_bits(m.positions[4], kSquare[0]));
  EXPECT_EQ(st.split_vertices, 1u);
  EXPECT_EQ(st.file_normals, 5u);
  EXPECT_EQ(st.computed_normals, 0u);

  BuildStats st2;
  const Mesh recomputed = build_mesh(s, ConvertOptions{true, true}, &st2);
  EXPECT_EQ(recomputed.vertex_count(), 4u);
  for (const Vec3f& n : recomputed.normals) EXPECT_TRUE(same_bits(n, Vec3f{0, 0, 1}));
  EXPECT_EQ(st2.file_normals, 0u);
}

TEST(BuildMesh, UnitFileNormalsAreKeptExactly) {
  // 0.57735026f is 1/sqrt(3) rounded to float; normalizing it again in double would change its last bit.
  const Vec3f diagonal{0.57735026f, 0.57735026f, 0.57735026f};
  SourceMesh s = source(kSquare, {{0, 1, 2}});
  s.normals = {diagonal, {0.6f, 0.8f, 0.0f}};
  set_normals(s, 0, {0, 1, 0});
  const Mesh m = build_mesh(s, {});
  EXPECT_TRUE(same_bits(m.normals[0], diagonal));
  EXPECT_TRUE(same_bits(m.normals[1], Vec3f{0.6f, 0.8f, 0.0f}));
}

TEST(BuildMesh, ZeroLengthFileNormalsAreComputedInstead) {
  SourceMesh s = source(kSquare, {{0, 1, 2}, {0, 2, 3}});
  s.normals = {{0, 0, 0}, {0, 0, 1}};
  set_normals(s, 0, {0, 0, 0});
  set_normals(s, 1, {1, 1, 1});
  BuildStats st;
  const Mesh m = build_mesh(s, {}, &st);
  EXPECT_EQ(m.vertex_count(), 4u);  // the computed normal equals the file's, so vertices 0 and 2 are shared
  for (const Vec3f& n : m.normals) EXPECT_TRUE(same_bits(n, Vec3f{0, 0, 1}));
  EXPECT_EQ(st.invalid_file_normals, 3u);
  EXPECT_EQ(st.computed_normals, 3u);
  EXPECT_EQ(st.file_normals, 1u);
}

TEST(BuildMesh, NormalsThatCancelTakeTheFirstTriangles) {
  // The same triangle twice, once with each winding: the computed normals sum to zero.
  BuildStats st;
  const Mesh m = build_mesh(source({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {{0, 1, 2}, {0, 2, 1}}), {}, &st);
  EXPECT_EQ(m.vertex_count(), 3u);
  for (const Vec3f& n : m.normals) EXPECT_TRUE(same_bits(n, Vec3f{0, 0, 1}));
  EXPECT_EQ(st.cancelled_normals, 3u);
  EXPECT_EQ(st.default_normals, 0u);
}

TEST(BuildMesh, VerticesWithOnlyZeroAreaTrianglesGetPlusZ) {
  // Three different points on a line: kept (no two corners coincide), but there is no direction to use.
  BuildStats st;
  const Mesh m = build_mesh(source({{0, 0, 0}, {1, 1, 0}, {2, 2, 0}}, {{0, 1, 2}}), {}, &st);
  for (const Vec3f& n : m.normals) EXPECT_TRUE(same_bits(n, Vec3f{0, 0, 1}));
  EXPECT_EQ(st.default_normals, 3u);
  EXPECT_EQ(st.cancelled_normals, 0u);
}

TEST(BuildMesh, ColorsSplitVerticesOrAreLeftOut) {
  SourceMesh s = source(kSquare, {{0, 1, 2}, {0, 2, 3}});
  s.colors = {{1, 0, 0}, {0, 1, 0}};
  set_colors(s, 0, {0, 0, 0});
  set_colors(s, 1, {1, 0, kNone});  // vertex 0 in a second color; a corner without color
  BuildStats st;
  const Mesh m = build_mesh(s, {}, &st);
  EXPECT_EQ(triangles_of(m), (std::vector<Tri>{{0, 1, 2}, {3, 2, 4}}));
  ASSERT_EQ(m.colors.size(), 5u);
  EXPECT_TRUE(same_bits(m.colors[0], Vec3f{1, 0, 0}));
  EXPECT_TRUE(same_bits(m.colors[3], Vec3f{0, 1, 0}));
  EXPECT_TRUE(same_bits(m.colors[4], kDefaultColor));
  EXPECT_EQ(st.split_vertices, 1u);

  const Mesh plain = build_mesh(s, ConvertOptions{false, false});
  EXPECT_FALSE(plain.has_colors());
  EXPECT_EQ(plain.vertex_count(), 4u);
}

const char* const kCubeObj =
    "mtllib cube.mtl\n"
    "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
    "usemtl red\n"
    "f 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\n"
    "usemtl blue\n"
    "f 2 3 7 6\nf 3 4 8 7\nf 4 1 5 8\n";

const char* const kCubePly =
    "ply\nformat ascii 1.0\nelement vertex 8\nproperty float x\nproperty float y\nproperty float z\n"
    "element face 6\nproperty list uchar int vertex_indices\nend_header\n"
    "-1 -1 -1\n1 -1 -1\n1 1 -1\n-1 1 -1\n-1 -1 1\n1 -1 1\n1 1 1\n-1 1 1\n"
    "4 0 3 2 1\n4 4 5 6 7\n4 0 1 5 4\n4 1 2 6 5\n4 2 3 7 6\n4 3 0 4 7\n";

TEST(BuildMesh, ObjAndPlyOfTheSameModelGiveTheSameMesh) {
  const Mesh from_obj = build_mesh(read_obj(kCubeObj, "", "cube.obj"), ConvertOptions{false, false});
  const Mesh from_ply = build_mesh(read_ply(kCubePly, "cube.ply"), {});
  expect_same_mesh(from_obj, from_ply);
  EXPECT_EQ(from_ply.triangle_count(), 12u);
  EXPECT_EQ(from_ply.vertex_count(), 8u);
  // Smooth normals of a cube's corners point diagonally outward.
  for (std::size_t v = 0; v < 8; ++v) {
    const Vec3f& p = from_ply.positions[v];
    const Vec3f& n = from_ply.normals[v];
    EXPECT_GT(p.x * n.x + p.y * n.y + p.z * n.z, 1.5f) << "vertex " << v;
  }
}

TEST(ConvertModel, ConvertsFilesEndToEnd) {
  TempDir dir;
  write_file(dir.file("cube.obj"), kCubeObj);
  write_file(dir.file("cube.mtl"), "newmtl red\nKd 1 0 0\nnewmtl blue\nKd 0 0 1\n");
  write_file(dir.file("cube.ply"), kCubePly);
  write_file(dir.file("cube_ply.dat"), kCubePly);  // PLY is recognized by its first line, too
  write_file(dir.file("cube.stl"), "solid cube\n");

  ConvertReport r;
  const Mesh colored = convert_model(dir.file("cube.obj"), {}, &r);
  EXPECT_EQ(r.format, "OBJ");
  EXPECT_EQ(r.source_vertices, 8u);
  EXPECT_EQ(r.source_faces, 6u);
  EXPECT_EQ(r.source_polygons, 6u);
  EXPECT_EQ(r.source_triangles, 12u);
  EXPECT_TRUE(r.warnings.empty());
  EXPECT_TRUE(colored.has_colors());
  EXPECT_EQ(colored.vertex_count(), 16u);  // every corner of the cube touches a red and a blue face

  const Bounds written = save_mesh(dir.file("cube.bin"), colored);
  test::expect_bin_holds(dir.file("cube.bin"), colored);
  EXPECT_TRUE(same_bits(written.max, Vec3f{1, 1, 1}));
  EXPECT_EQ(written.radius, compute_bounds(colored.positions).radius);

  const Mesh a = convert_model(dir.file("cube.ply"), {});
  const Mesh b = convert_model(dir.file("cube_ply.dat"), {});
  expect_same_mesh(a, b);
  expect_error<ConvertError>([&] { convert_model(dir.file("cube.stl"), {}); }, "unsupported input");
  expect_error<ConvertError>([&] { convert_model(dir.file("none.obj"), {}); }, "cannot open");
  std::filesystem::create_directory(dir.file("folder.obj"));
  expect_error<ConvertError>([&] { convert_model(dir.file("folder.obj"), {}); }, "is not a regular file");
}

// The converter's promises about its output, on a messy model: duplicate and degenerate triangles,
// unused vertices, some file normals (one of zero length), colors, polygons.
TEST(ConvertModel, OutputKeepsTheConvertersGuarantees) {
  std::mt19937 rng(3);
  const std::uint32_t n = 40;
  std::uniform_int_distribution<int> pick(0, 9);
  SourceMesh s;
  for (std::uint32_t i = 0; i < n * n; ++i) {
    const float jitter = static_cast<float>(pick(rng)) * 0.01f;
    s.positions.push_back(Vec3f{static_cast<float>(i % n), static_cast<float>(i / n), jitter});
  }
  s.positions.push_back(s.positions[5]);  // a second vertex at an existing position
  s.normals = {{0, 0, 1}, {0, 1, 1}, {0, 0, 0}};
  s.colors = {{1, 0, 0}, {0.5f, 0.5f, 0.5f}};
  const auto corner = [&](std::uint32_t p) {
    Corner c;
    c.position = p;
    const int r = pick(rng);
    c.normal = r < 3 ? static_cast<std::uint32_t>(r) : kNone;
    c.color = r % 2 == 0 ? 0u : 1u;
    return c;
  };
  for (std::uint32_t y = 0; y + 1 < n; y += 3) {  // rows 2, 5, 8, ... stay unused
    for (std::uint32_t x = 0; x + 1 < n; ++x) {
      const std::uint32_t a = y * n + x, b = a + 1, c = a + n + 1, d = a + n;
      for (const Tri& t : {Tri{a, b, c}, Tri{a, c, d}, Tri{a, a, b}, Tri{a, b, c}}) {
        for (const std::uint32_t p : t) s.corners.push_back(corner(p));
      }
    }
  }
  for (const std::uint32_t p : {n * n, n + 6, n + 5}) s.corners.push_back(corner(p));  // n*n is at vertex 5's position

  BuildStats st;
  const Mesh m = build_mesh(s, {}, &st);
  EXPECT_NO_THROW(validate_mesh(m));
  EXPECT_GT(st.degenerate_triangles, 0u);
  EXPECT_GT(st.unused_positions, 0u);
  EXPECT_GT(st.split_vertices, 0u);
  EXPECT_GT(st.invalid_file_normals, 0u);

  // Vertices are numbered in order of first use, so each new index is the next one, and none is unused.
  std::uint32_t seen = 0;
  for (const Triangle& t : m.triangles) {
    for (const std::uint32_t v : t.v) {
      ASSERT_LE(v, seen);
      if (v == seen) ++seen;
    }
    const Vec3f &a = m.positions[t.v[0]], &b = m.positions[t.v[1]], &c = m.positions[t.v[2]];
    EXPECT_FALSE(same_bits(a, b) || same_bits(b, c) || same_bits(a, c));
  }
  EXPECT_EQ(seen, m.vertex_count());

  // The same input always gives the same mesh.
  expect_same_mesh(build_mesh(s, {}), m);
}

}  // namespace
}  // namespace rasty::convert
