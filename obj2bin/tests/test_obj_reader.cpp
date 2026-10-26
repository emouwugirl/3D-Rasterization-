// The OBJ reader: statements, corner forms, materials, warnings and errors.
#include <gtest/gtest.h>

#include <clocale>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "convert.hpp"
#include "test_util.hpp"

namespace rasty::convert {
namespace {

using test::expect_error;
using test::TempDir;
using test::write_file;

SourceMesh obj(const std::string& text, const std::string& base_dir = "") { return read_obj(text, base_dir, "t.obj"); }

std::vector<std::uint32_t> positions_of(const SourceMesh& m) {
  std::vector<std::uint32_t> out;
  for (const Corner& c : m.corners) out.push_back(c.position);
  return out;
}

bool has_warning(const SourceMesh& m, const std::string& part) {
  for (const std::string& w : m.warnings) {
    if (w.find(part) != std::string::npos) return true;
  }
  return false;
}

const char* const kSquare = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n";

TEST(ObjReader, PolygonsAreFanTriangulated) {
  const SourceMesh m = obj(std::string(kSquare) + "v 0.5 2 0\nf 1 2 3\nf 1 2 3 4\nf 1 2 3 5 4\n");
  EXPECT_EQ(m.format, "OBJ");
  EXPECT_EQ(m.positions.size(), 5u);
  EXPECT_EQ(m.faces, 3u);
  EXPECT_EQ(m.polygons, 2u);
  EXPECT_EQ(positions_of(m), (std::vector<std::uint32_t>{0, 1, 2, 0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 4, 0, 4, 3}));
  EXPECT_TRUE(m.warnings.empty());
}

TEST(ObjReader, CornerFormsAndRelativeIndices) {
  const SourceMesh m = obj(std::string(kSquare) +
                           "vt 0 0\nvt 1 0\nvt 1 1\n"
                           "vn 0 0 1\nvn 0 0 2\n"
                           "f 1/1/1 2/2/2 3/3/1\n"  // v/vt/vn
                           "f 1//2 3//2 4//2\n"     // v//vn
                           "f 1/1 2/2 3/3\n"        // v/vt
                           "f -4 -3 -2\n"           // relative to the 4 vertices so far
                           "f -4//-1 -3//-2 -1//-1\n");
  ASSERT_EQ(m.corners.size(), 15u);
  EXPECT_EQ(positions_of(m), (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 1, 2, 0, 1, 3}));
  const std::uint32_t normals[] = {0, 1, 0, 1, 1, 1, kNone, kNone, kNone, kNone, kNone, kNone, 1, 0, 1};
  for (std::size_t i = 0; i < 15; ++i) EXPECT_EQ(m.corners[i].normal, normals[i]) << "corner " << i;
  ASSERT_EQ(m.normals.size(), 2u);
  EXPECT_EQ(m.normals[1].z, 2.0f);  // stored as written; build_mesh() normalizes
  for (const Corner& c : m.corners) EXPECT_EQ(c.color, kNone);
  EXPECT_TRUE(m.colors.empty());
}

TEST(ObjReader, FacesMayComeBeforeTheirVertices) {
  const SourceMesh m = obj("f 1 2 3\nv 0 0 0\nv 1 0 0\nv 0 1 0\n");
  EXPECT_EQ(positions_of(m), (std::vector<std::uint32_t>{0, 1, 2}));
}

TEST(ObjReader, BadInputGivesTheLine) {
  const std::string v = kSquare;  // lines 1-4
  expect_error<ConvertError>([&] { obj(v + "f 0 1 2\n"); }, "t.obj:5: vertex index 0 is invalid");
  expect_error<ConvertError>([&] { obj(v + "f 1 2 3\nf 1 2 9\n"); }, "t.obj:6: a face uses vertex 9, but the file defines 4");
  expect_error<ConvertError>([&] { obj(v + "f -5 1 2\n"); }, "t.obj:5: relative vertex index -5 points before the first vertex");
  expect_error<ConvertError>([&] { obj(v + "vn 0 0 1\nf 1//1 2//2 3//1\n"); }, "t.obj:6: a face uses normal 2, but the file defines 1");
  expect_error<ConvertError>([&] { obj(v + "f 1 2 x\n"); }, "t.obj:5: malformed face corner");
  expect_error<ConvertError>([&] { obj(v + "f 1/a 2 3\n"); }, "t.obj:5: malformed");
  expect_error<ConvertError>([&] { obj(v + "v 1 2 zz\n"); }, "t.obj:5: invalid number after 'v'");
  expect_error<ConvertError>([&] { obj(v + "v 1 2\n"); }, "t.obj:5: expected x y z after 'v'");
  expect_error<ConvertError>([&] { obj(v + "v 1 nan 2\n"); }, "t.obj:5: vertex position is not finite");
  expect_error<ConvertError>([&] { obj(v + "v 1e300 0 0\n"); }, "t.obj:5: vertex position is not finite");  // beyond float
  expect_error<ConvertError>([&] { obj(v + "v 1 2 3 4 5\n"); }, "t.obj:5: a 'v' statement has 3, 4, 6 or 7 values");
  expect_error<ConvertError>([&] { obj(v + "vn 0 1\n"); }, "t.obj:5: expected x y z after 'vn'");
  expect_error<ConvertError>([&] { obj(v); }, "t.obj: no triangles");
  expect_error<ConvertError>([&] { obj(v + "f 1 2\n"); }, "t.obj: no triangles");
}

TEST(ObjReader, BadTextureCoordinateReferencesAreOnlyWarnings) {
  // Texture coordinates are not kept, so faces that refer to missing ones still convert.
  const SourceMesh m = obj(std::string(kSquare) + "vt 0 0\nf 1/1 2/7 3/1\nf 1/0 3/1 4/1\n");  // lines 6 and 7
  EXPECT_EQ(positions_of(m), (std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3}));
  EXPECT_TRUE(has_warning(m, "faces refer to texture coordinates the file does not define (e.g. line 7)"));
  EXPECT_TRUE(obj(std::string(kSquare) + "vt 0 0\nf 1/1 2/1 3/1\n").warnings.empty());
}

TEST(ObjReader, SummaryWarningsSurviveTheCap) {
  TempDir dir;
  std::string mtl = "newmtl a\nmap_Kd a.png\n";
  for (int i = 0; i < 25; ++i) mtl += "Kd spectral a.rfl\n";  // 25 warnings about single lines
  write_file(dir.file("m.mtl"), mtl);
  const SourceMesh m = obj(std::string("mtllib m.mtl\nusemtl missing\n") + kSquare + "f 1 2\nf 1 2 3\n", dir.dir());
  EXPECT_TRUE(has_warning(m, "(5 more warnings)"));
  EXPECT_TRUE(has_warning(m, "skipped 1 faces with fewer than 3 corners"));
  EXPECT_TRUE(has_warning(m, "textures (map_Kd) are not supported"));
  EXPECT_TRUE(has_warning(m, "1 materials are used but not defined in any MTL file ('missing')"));
  EXPECT_EQ(m.warnings.size(), 20u + 1u + 3u);
}

TEST(ObjReader, IgnoredStatementsAreReported) {
  const SourceMesh m = obj(std::string(kSquare) +
                           "o thing\ng group\ns 1\nmg 1\n"  // silently ignored
                           "l 1 2 3\np 4\n"                 // not triangles
                           "bevel on\nbevel off\n"          // unknown: one warning per keyword
                           "v 2 2 2 0.5\n"                  // w coordinate
                           "f 1 2\n"                        // too few corners
                           "f 1 2 3\n");
  EXPECT_EQ(m.corners.size(), 3u);
  EXPECT_EQ(m.skipped_faces, 1u);
  EXPECT_EQ(m.warnings.size(), 4u);
  EXPECT_TRUE(has_warning(m, "t.obj:11: ignoring unsupported statement 'bevel'"));
  EXPECT_TRUE(has_warning(m, "ignored 2 line and point statements"));
  EXPECT_TRUE(has_warning(m, "ignored the w coordinate of 1 vertices"));
  EXPECT_TRUE(has_warning(m, "skipped 1 faces with fewer than 3 corners (the first on line 14)"));
}

TEST(ObjReader, LineEndingsCommentsAndByteOrderMark) {
  const SourceMesh m = obj("\xEF\xBB\xBF# comment\r\nv\t0 0 0 \r\nv 1 0 0 # trailing\r\n\r\n  v 0 1 0\r\nf 1 2 3\r\n");
  EXPECT_EQ(m.positions.size(), 3u);
  EXPECT_EQ(m.positions[2].y, 1.0f);
  EXPECT_EQ(positions_of(m), (std::vector<std::uint32_t>{0, 1, 2}));
  EXPECT_TRUE(m.warnings.empty());
}

TEST(ObjReader, NumbersAreReadExactly) {
  const SourceMesh m = obj("v 0.1 -1e-3 3.4028234e38\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
  EXPECT_EQ(m.positions[0].x, 0.1f);
  EXPECT_EQ(m.positions[0].y, -1e-3f);
  EXPECT_EQ(m.positions[0].z, 3.4028234e38f);
}

TEST(ObjReader, VertexColorsAreConvertedToLinear) {
  const SourceMesh m = obj("v 0 0 0 1 0.5 0\nv 1 0 0 0 0 1\nv 0 1 0 0.04045 1 1\nf 1 2 3\n");
  ASSERT_EQ(m.colors.size(), 3u);
  EXPECT_EQ(m.colors[0].x, 1.0f);
  EXPECT_NEAR(m.colors[0].y, 0.214041, 1e-6);
  EXPECT_EQ(m.colors[0].z, 0.0f);
  EXPECT_NEAR(m.colors[2].x, 0.04045 / 12.92, 1e-7);
  for (std::size_t k = 0; k < 3; ++k) EXPECT_EQ(m.corners[k].color, k);
}

TEST(ObjReader, SrgbToLinear) {
  EXPECT_EQ(srgb_to_linear(0.0f), 0.0f);
  EXPECT_EQ(srgb_to_linear(1.0f), 1.0f);
  EXPECT_EQ(srgb_to_linear(2.0f), 1.0f);   // clamped
  EXPECT_EQ(srgb_to_linear(-1.0f), 0.0f);  // clamped
  EXPECT_NEAR(srgb_to_linear(0.5f), 0.214041, 1e-6);
  EXPECT_NEAR(srgb_to_linear(0.04045f), 0.0031308, 1e-7);
  // It avoids std::pow (for determinism) but must agree with the sRGB formula: to within one float step
  // for every 8-bit value.
  for (int i = 0; i <= 255; ++i) {
    const float c = static_cast<float>(i / 255.0);
    const double v = c;
    const float expected = static_cast<float>(v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4));
    const float got = srgb_to_linear(c);
    EXPECT_LE(std::fabs(got - expected), std::nextafter(expected, 2.0f) - expected) << i;
  }
}

TEST(ObjReader, NumbersDoNotDependOnTheLocale) {
  // 1.5e30 and the 22-digit number take the strtod path, which follows the C locale's decimal point.
  const std::string text = "v 1.5e30 0.1234567890123456789012 -2.5e-30\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  const SourceMesh c_locale = obj(text);
  const std::string saved = std::setlocale(LC_NUMERIC, nullptr);
  const char* comma = nullptr;
  for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8", "de_DE"}) {
    if (std::setlocale(LC_NUMERIC, name)) {
      comma = name;
      break;
    }
  }
  if (!comma) GTEST_SKIP() << "no locale with a decimal comma is installed";
  const SourceMesh other = obj(text);
  std::setlocale(LC_NUMERIC, saved.c_str());
  EXPECT_EQ(other.positions[0].x, 1.5e30f);
  EXPECT_EQ(other.positions[0].y, 0.12345679f);
  EXPECT_EQ(other.positions[0].z, -2.5e-30f);
  EXPECT_TRUE(test::same_bits(other.positions[0], c_locale.positions[0]));
}

TEST(ObjReader, MaterialColorsBecomeCornerColors) {
  TempDir dir;
  write_file(dir.file("m.mtl"),
             "newmtl red\nKd 1 0 0\nmap_Kd red.png\n"
             "newmtl gray\nKd 0.25\n"
             "newmtl blank\n");  // no Kd: default color
  const SourceMesh m = obj(std::string("mtllib m.mtl\n") + kSquare +
                               "f 1 2 3\n"            // before any usemtl: default color
                               "usemtl red\nf 1 2 3\n"
                               "usemtl gray\nf 1 3 4\n"
                               "usemtl blank\nf 1 3 4\n"
                               "usemtl nowhere\nf 2 3 4\n",  // not defined anywhere: default color
                           dir.dir());
  ASSERT_EQ(m.corners.size(), 15u);
  const auto color_of = [&](std::size_t corner) { return m.colors.at(m.corners[corner].color); };
  EXPECT_TRUE(test::same_bits(color_of(0), kDefaultColor));
  EXPECT_TRUE(test::same_bits(color_of(3), Vec3f{1, 0, 0}));
  EXPECT_TRUE(test::same_bits(color_of(6), Vec3f{0.25f, 0.25f, 0.25f}));  // Kd is already linear
  EXPECT_TRUE(test::same_bits(color_of(9), kDefaultColor));
  EXPECT_TRUE(test::same_bits(color_of(12), kDefaultColor));
  for (std::size_t t = 0; t < 5; ++t) {
    EXPECT_EQ(m.corners[3 * t].color, m.corners[3 * t + 1].color);
    EXPECT_EQ(m.corners[3 * t].color, m.corners[3 * t + 2].color);
  }
  EXPECT_TRUE(has_warning(m, "1 materials are used but not defined in any MTL file ('nowhere')"));
  EXPECT_TRUE(has_warning(m, "textures (map_Kd) are not supported"));
}

TEST(ObjReader, VertexColorsWinOverMaterials) {
  TempDir dir;
  write_file(dir.file("m.mtl"), "newmtl red\nKd 1 0 0\n");
  const SourceMesh m = obj("mtllib m.mtl\nusemtl red\nv 0 0 0 0 0 1\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", dir.dir());
  EXPECT_TRUE(test::same_bits(m.colors.at(m.corners[0].color), Vec3f{0, 0, 1}));
  EXPECT_TRUE(test::same_bits(m.colors.at(m.corners[1].color), Vec3f{1, 0, 0}));
  EXPECT_TRUE(has_warning(m, "only 1 of 3 vertices have colors"));
}

TEST(ObjReader, AbsoluteMaterialLibraryPathsAreUsedAsGiven) {
  TempDir dir;
  write_file(dir.file("abs.mtl"), "newmtl red\nKd 1 0 0\n");
  const std::string absolute = std::filesystem::absolute(dir.file("abs.mtl")).string();
  const SourceMesh m = obj("mtllib " + absolute + "\nusemtl red\n" + kSquare + "f 1 2 3\n", "/some/other/dir/");
  EXPECT_TRUE(m.warnings.empty());
  ASSERT_FALSE(m.colors.empty());
  EXPECT_TRUE(test::same_bits(m.colors.at(m.corners[0].color), Vec3f{1, 0, 0}));
}

TEST(ObjReader, MaterialLibraryThatIsADirectoryIsAWarning) {
  TempDir dir;
  std::filesystem::create_directory(dir.file("textures"));
  const SourceMesh m = obj(std::string("mtllib textures\n") + kSquare + "f 1 2 3\n", dir.dir());
  EXPECT_TRUE(has_warning(m, "cannot read material library"));
}

TEST(ObjReader, MissingMaterialFilesAreWarnings) {
  TempDir dir;
  const SourceMesh m = obj(std::string("mtllib none.mtl\nusemtl red\n") + kSquare + "f 1 2 3\n", dir.dir());
  EXPECT_TRUE(has_warning(m, "cannot read material library"));
  EXPECT_TRUE(has_warning(m, "('red')"));
  EXPECT_TRUE(m.colors.empty());  // no material is defined, so the model has no colors
  EXPECT_EQ(m.corners[0].color, kNone);
}

}  // namespace
}  // namespace rasty::convert
