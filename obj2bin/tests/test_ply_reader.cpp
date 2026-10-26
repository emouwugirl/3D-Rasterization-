// The PLY reader: ASCII and both binary byte orders, skipped properties and elements, errors.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "convert.hpp"
#include "test_util.hpp"

namespace rasty::convert {
namespace {

using test::expect_error;

// Writes PLY data in any of the three formats from the same sequence of values.
class PlyWriter {
 public:
  explicit PlyWriter(std::string format) : format_(std::move(format)) {}

  template <typename T>
  PlyWriter& operator<<(T v) {
    if (format_ == "ascii") {
      if (!row_start_) out_ += ' ';
      if constexpr (std::is_floating_point_v<T>) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v));
        out_ += buf;
      } else {
        out_ += std::to_string(+v);
      }
      row_start_ = false;
    } else {
      char b[sizeof(T)];
      std::memcpy(b, &v, sizeof v);
      if (format_ == "binary_big_endian") std::reverse(b, b + sizeof v);  // the tests run on little-endian hosts
      out_.append(b, sizeof v);
    }
    return *this;
  }

  void end_row() {
    if (format_ == "ascii") out_ += '\n';
    row_start_ = true;
  }

  const std::string& str() const { return out_; }

 private:
  std::string format_;
  std::string out_;
  bool row_start_ = true;
};

// Four vertices of a unit square with extra properties, three faces (a triangle, a quad and one with too
// few corners), and two elements the reader does not use, one before and one after the mesh.
std::string square_ply(const std::string& format) {
  std::string ply = "ply\nformat " + format +
                    " 1.0\n"
                    "comment made by a test\nobj_info nothing\n"
                    "element info 2\nproperty list uchar float values\n"
                    "element vertex 4\nproperty float x\nproperty float y\nproperty float z\n"
                    "property float confidence\nproperty list uchar int extra\n"
                    "element face 3\nproperty list uchar int vertex_indices\nproperty int flags\n"
                    "element edge 2\nproperty int vertex1\nproperty int vertex2\nproperty uchar crease\n"
                    "end_header\n";
  PlyWriter w(format);
  w << std::uint8_t{2} << 0.5f << 1.5f;
  w.end_row();
  w << std::uint8_t{0};
  w.end_row();
  const float xy[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  for (const auto& p : xy) {
    w << p[0] << p[1] << 0.25f << 0.9f << std::uint8_t{1} << std::int32_t{7};
    w.end_row();
  }
  w << std::uint8_t{3} << std::int32_t{0} << std::int32_t{1} << std::int32_t{2} << std::int32_t{0};
  w.end_row();
  w << std::uint8_t{4} << std::int32_t{0} << std::int32_t{1} << std::int32_t{2} << std::int32_t{3} << std::int32_t{0};
  w.end_row();
  w << std::uint8_t{2} << std::int32_t{3} << std::int32_t{2} << std::int32_t{0};
  w.end_row();
  for (int e = 0; e < 2; ++e) {
    w << std::int32_t{e} << std::int32_t{e + 1} << std::uint8_t{0};
    w.end_row();
  }
  return ply + w.str();
}

std::vector<std::uint32_t> positions_of(const SourceMesh& m) {
  std::vector<std::uint32_t> out;
  for (const Corner& c : m.corners) out.push_back(c.position);
  return out;
}

TEST(PlyReader, ReadsEveryFormatTheSame) {
  for (const std::string format : {"ascii", "binary_little_endian", "binary_big_endian"}) {
    SCOPED_TRACE(format);
    const SourceMesh m = read_ply(square_ply(format), "t.ply");
    EXPECT_EQ(m.format, "PLY " + format);
    ASSERT_EQ(m.positions.size(), 4u);
    EXPECT_TRUE(test::same_bits(m.positions[2], Vec3f{1, 1, 0.25f}));
    EXPECT_EQ(positions_of(m), (std::vector<std::uint32_t>{0, 1, 2, 0, 1, 2, 0, 2, 3}));
    EXPECT_EQ(m.faces, 3u);
    EXPECT_EQ(m.polygons, 1u);
    EXPECT_EQ(m.skipped_faces, 1u);
    ASSERT_EQ(m.warnings.size(), 1u);
    EXPECT_NE(m.warnings[0].find("skipped 1 faces with fewer than 3 corners (the first is face #2)"), std::string::npos);
    EXPECT_TRUE(m.normals.empty());
    EXPECT_TRUE(m.colors.empty());
    for (const Corner& c : m.corners) {
      EXPECT_EQ(c.normal, kNone);
      EXPECT_EQ(c.color, kNone);
    }
  }
}

TEST(PlyReader, NormalsAndColors) {
  const std::string header =
      "ply\r\nformat ascii 1.0\r\nelement vertex 3\r\n"
      "property float x\r\nproperty float y\r\nproperty float z\r\n"
      "property float nx\r\nproperty float ny\r\nproperty float nz\r\n";
  const std::string faces = "element face 1\r\nproperty list uchar uint vertex_indices\r\nend_header\r\n";
  {
    const SourceMesh m = read_ply(header +
                                      "property uchar red\r\nproperty uchar green\r\nproperty uchar blue\r\n" + faces +
                                      "0 0 0 0 0 2 255 0 128\r\n1 0 0 0 0 1 0 255 0\r\n0 1 0 0 0 1 0 0 0\r\n3 0 1 2\r\n",
                                  "t.ply");
    ASSERT_EQ(m.normals.size(), 3u);
    EXPECT_TRUE(test::same_bits(m.normals[0], Vec3f{0, 0, 2}));  // as written; build_mesh() normalizes
    ASSERT_EQ(m.colors.size(), 3u);
    EXPECT_EQ(m.colors[0].x, 1.0f);
    EXPECT_EQ(m.colors[0].y, 0.0f);
    EXPECT_EQ(m.colors[0].z, srgb_to_linear(128.0f / 255.0f));
    for (std::size_t k = 0; k < 3; ++k) {
      EXPECT_EQ(m.corners[k].normal, k);
      EXPECT_EQ(m.corners[k].color, k);
    }
  }
  {
    const SourceMesh m = read_ply(header +
                                      "property float red\r\nproperty float green\r\nproperty float blue\r\n" + faces +
                                      "0 0 0 0 0 1 0.5 1 0\r\n1 0 0 0 0 1 0 0 0\r\n0 1 0 0 0 1 0 0 0\r\n3 0 1 2\r\n",
                                  "t.ply");
    EXPECT_EQ(m.colors[0].x, srgb_to_linear(0.5f));
    EXPECT_EQ(m.colors[0].y, 1.0f);
  }
}

TEST(PlyReader, BadFilesAreErrors) {
  const std::string v3 =
      "element vertex 3\nproperty float x\nproperty float y\nproperty float z\n";
  const std::string f1 = "element face 1\nproperty list uchar int vertex_indices\n";
  const std::string head = "ply\nformat ascii 1.0\n";
  const std::string rows = "0 0 0\n1 0 0\n0 1 0\n";
  const auto ply = [](const std::string& text) { return read_ply(text, "t.ply"); };

  expect_error<ConvertError>([&] { ply("plx\n"); }, "t.ply: not a PLY file");
  expect_error<ConvertError>([&] { ply("ply\n" + v3 + f1 + "end_header\n"); }, "missing format line");
  expect_error<ConvertError>([&] { ply("ply\nformat text 1.0\n"); }, "unknown format 'text'");
  expect_error<ConvertError>([&] { ply(head + v3 + f1); }, "missing end_header");
  expect_error<ConvertError>([&] { ply(head + f1 + "end_header\n3 0 1 2\n"); }, "no 'vertex' element");
  expect_error<ConvertError>([&] { ply(head + v3 + "end_header\n" + rows); }, "no 'face' element");
  expect_error<ConvertError>([&] { ply(head + v3 + "element face 1\nproperty list uchar int idx\nend_header\n" + rows + "3 0 1 2\n"); },
                             "no 'vertex_indices' list");
  expect_error<ConvertError>([&] { ply(head + "element vertex 1\nproperty float x\nproperty float y\n" + f1 + "end_header\n0 0\n3 0 0 0\n"); },
                             "lacks x, y or z");
  expect_error<ConvertError>([&] { ply(head + v3 + f1 + "end_header\n" + rows + "3 0 1 3\n"); },
                             "element 'face' #0: vertex index 3 is out of range (the file has 3 vertices)");
  expect_error<ConvertError>([&] { ply(head + v3 + f1 + "end_header\n" + rows + "3 0 -1 2\n"); }, "vertex index -1 is out of range");
  expect_error<ConvertError>([&] { ply(head + v3 + f1 + "end_header\n0 0 0\n1 abc 0\n0 1 0\n3 0 1 2\n"); },
                             "element 'vertex' #1: invalid number 'abc'");
  expect_error<ConvertError>([&] { ply(head + v3 + f1 + "end_header\n0 0 0\n1 nan 0\n0 1 0\n3 0 1 2\n"); },
                             "element 'vertex' #1: vertex position is not finite");
  expect_error<ConvertError>([&] { ply(head + v3 + f1 + "end_header\n0 0 0\n1 1e300 0\n0 1 0\n3 0 1 2\n"); },
                             "element 'vertex' #1: vertex position is not finite");  // beyond float's range
  expect_error<ConvertError>([&] { ply(head + v3 + f1 + "end_header\n" + rows + "3 0 1\n"); }, "unexpected end of data");
  expect_error<ConvertError>([&] { ply(head + v3 + f1 + "end_header\n" + rows + "200 0 1 2\n"); }, "longer than the rest of the file");
  expect_error<ConvertError>([&] { ply(head + v3 + f1 + "end_header\n" + rows + "2 0 1\n"); }, "t.ply: no triangles");

  // Binary data that stops before the face's last property.
  const std::string bin = "ply\nformat binary_little_endian 1.0\n" + v3 + f1 + "property int flags\nend_header\n";
  expect_error<ConvertError>([&] { ply(bin + std::string(36, '\0') + std::string(1, '\x03') + std::string(12, '\0')); },
                             "element 'face' #0: unexpected end of data");
  expect_error<ConvertError>([&] { ply("ply\nformat binary_little_endian 1.0\nelement vertex 1000000\nproperty float x\n"
                                       "property float y\nproperty float z\n" + f1 + "end_header\n" + std::string(64, '\0')); },
                             "more than the file can hold");
}

}  // namespace
}  // namespace rasty::convert
