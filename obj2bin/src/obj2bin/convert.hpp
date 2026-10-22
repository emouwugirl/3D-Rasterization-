// The obj2bin converter: OBJ and PLY models -> rasty::Mesh, ready to be saved as a .bin file.
//
// Conversion happens in two steps:
//   1. A reader (read_obj, read_ply) turns the file into a SourceMesh: the file's own vertices, normals
//      and colors, and its faces split into triangles of corners that point into those lists.
//   2. build_mesh() turns that into the Mesh the renderers draw: one index per corner, a unit normal
//      and (optionally) a color for every vertex, and only triangles that can cover pixels.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "rasty/bin_format.hpp"

namespace rasty::convert {

// Thrown for unreadable or malformed input; the message names the file (and line, where known).
class ConvertError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

inline constexpr std::uint32_t kNone = 0xFFFFFFFFu;

// Color for faces without one in a model that has colors elsewhere.
inline constexpr Vec3f kDefaultColor{0.8f, 0.8f, 0.8f};

// One triangle corner, as indices into a SourceMesh's lists.
struct Corner {
  std::uint32_t position = 0;
  std::uint32_t normal = kNone;  // into SourceMesh::normals, or kNone: compute one
  std::uint32_t color = kNone;   // into SourceMesh::colors, or kNone: no color
};

// A model as its file describes it, with polygons already split into triangles.
struct SourceMesh {
  std::string format;                // "OBJ", "PLY ascii", "PLY binary_little_endian", ...
  std::vector<Vec3f> positions;
  std::vector<Vec3f> normals;        // as written in the file (not necessarily unit length)
  std::vector<Vec3f> colors;         // linear RGB in [0, 1]
  std::vector<Corner> corners;       // three per triangle, in file order
  std::uint64_t faces = 0;           // faces read
  std::uint64_t polygons = 0;        // faces with more than three corners (fan-triangulated)
  std::uint64_t skipped_faces = 0;   // faces with fewer than three corners
  std::vector<std::string> warnings;

  std::size_t triangle_count() const { return corners.size() / 3; }
};

// `text` is the OBJ file's contents; MTL files named by mtllib are read from `base_dir`. `name` is used
// in messages.
SourceMesh read_obj(std::string_view text, const std::string& base_dir, const std::string& name);

// `data` is the PLY file's contents (ASCII or binary).
SourceMesh read_ply(std::string_view data, const std::string& name);

struct ConvertOptions {
  bool recompute_normals = false;  // ignore the file's normals and compute all of them
  bool keep_colors = true;         // false: leave colors out of the mesh
};

struct BuildStats {
  std::uint64_t degenerate_triangles = 0;  // removed: two corners at the same position
  std::uint64_t unused_positions = 0;      // file vertices that no remaining triangle uses
  std::uint64_t split_vertices = 0;        // extra vertices for positions with several normals or colors
  std::uint64_t file_normals = 0;          // vertices whose normal came from the file
  std::uint64_t computed_normals = 0;      // vertices whose normal was computed
  std::uint64_t invalid_file_normals = 0;  // file normals of zero length (computed ones used instead)
  std::uint64_t cancelled_normals = 0;     // computed normals that summed to zero; first triangle's used
  std::uint64_t default_normals = 0;       // vertices whose triangles all have zero area; set to +Z
};

Mesh build_mesh(const SourceMesh& source, const ConvertOptions& options, BuildStats* stats = nullptr);

// What convert_model() did, for printing. Everything up to `warnings` is filled in as soon as the file
// has been read, so the warnings are available even when building the mesh fails.
struct ConvertReport {
  std::string format;
  std::uint64_t input_bytes = 0;
  std::uint64_t source_vertices = 0;
  std::uint64_t source_faces = 0;
  std::uint64_t source_polygons = 0;
  std::uint64_t skipped_faces = 0;
  std::uint64_t source_triangles = 0;
  BuildStats build;
  std::vector<std::string> warnings;
  double read_seconds = 0.0;   // reading and parsing the file
  double build_seconds = 0.0;  // build_mesh()
};

// Reads an .obj or .ply file (chosen by extension, or by content for other names) and builds its Mesh.
Mesh convert_model(const std::string& path, const ConvertOptions& options, ConvertReport* report = nullptr);

// sRGB-encoded value -> linear, clamped to [0, 1] (vertex colors in model files are display colors).
// Computed from basic arithmetic only, so it gives the same bits on every machine.
float srgb_to_linear(float c);

// The whole file as bytes. Throws ConvertError if it is missing, not a regular file, or unreadable.
std::string read_file(const std::string& path);

}  // namespace rasty::convert
