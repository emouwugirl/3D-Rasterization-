// The .bin mesh format that obj2bin writes, and the writer itself.
//
// A .bin file holds one triangle mesh in exactly the form the renderers draw it: arrays that can be read
// straight into memory, with no parsing and no per-vertex work. docs/bin-format.md explains the format
// and its choices.
//
// File layout (version 2). All values are little-endian. Every array starts at a multiple of 64 bytes;
// the bytes between arrays are zero.
//
//   offset  size        contents
//   0       128         Header (below)
//   128     16 * V      positions: V x float32[4] (x, y, z, 1), object space, as in the source model
//   ...     16 * V      normals:   V x float32[4] (x, y, z, 0), unit length, one per vertex
//   ...     16 * V      colors:    V x float32[4] (r, g, b, 1), linear RGB albedo in [0, 1]
//                                  (only if flags & kFlagColors)
//   ...     16 * T      indices:   T x uint32[4] (a, b, c, 0), one triangle each; counter-clockwise seen
//                                  from the front
//
// Every element is padded to a 16-byte record so that each is one aligned 128-bit load: a CUDA float4
// or uint4, or one SSE/NEON register. Parallel readers (a GPU warp, SIMD lanes) then fetch consecutive
// records with fully coalesced, never-split loads. The fourth component is useful, not just filler:
// w = 1 makes a position a homogeneous point and w = 0 makes a normal a direction, so one 4x4 matrix
// transforms both.
//
// Every header offset is where its array starts, so a reader only needs the header to find the data.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace rasty {

// Same layout as CUDA's float3 and uint3.
struct Vec3f {
  float x, y, z;
};

// Three indices into the vertex arrays. The corners are counter-clockwise when the triangle is seen from
// its front side, and vertex normals point to that side.
struct Triangle {
  std::uint32_t v[3];
};

static_assert(sizeof(Vec3f) == 12 && alignof(Vec3f) == 4, "Vec3f must match float3 and the file layout");
static_assert(sizeof(Triangle) == 12 && alignof(Triangle) == 4, "Triangle must match uint3 and the file layout");

// Bounds of the vertex positions, for placing the camera.
struct Bounds {
  Vec3f min{0.0f, 0.0f, 0.0f};     // smallest x, y and z of any vertex
  Vec3f max{0.0f, 0.0f, 0.0f};     // largest x, y and z of any vertex
  Vec3f center{0.0f, 0.0f, 0.0f};  // center of that box
  float radius = 0.0f;             // every vertex lies within this distance of center
};

// A mesh as it is stored in a .bin file. Triangle i is triangles[i]; its corners are the vertices with
// those indices, and vertex k is positions[k], normals[k] and (if present) colors[k].
struct Mesh {
  std::vector<Vec3f> positions;
  std::vector<Vec3f> normals;      // one per vertex, unit length
  std::vector<Vec3f> colors;       // empty, or one linear RGB albedo per vertex
  std::vector<Triangle> triangles;

  std::size_t vertex_count() const { return positions.size(); }
  std::size_t triangle_count() const { return triangles.size(); }
  bool has_colors() const { return !colors.empty(); }
};

// Thrown for meshes that break the format's rules and for files that cannot be written.
class MeshError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Checks the mesh, then writes it as a .bin file with bounds computed from its positions, and returns
// those bounds. The data goes to a temporary file that is flushed to disk before it is renamed to
// `path`, so `path` never holds a partial file, even after a crash.
Bounds save_mesh(const std::string& path, const Mesh& mesh);

// Throws MeshError if the mesh breaks a rule of the format: at least one vertex and one triangle, one
// normal (and color, if any) per vertex, indices less than the vertex count, finite values, unit-length
// normals and colors in [0, 1].
void validate_mesh(const Mesh& mesh);

// Bounds of a set of positions (all zero for an empty set).
Bounds compute_bounds(const std::vector<Vec3f>& positions);

// The on-disk layout.
namespace bin {

inline constexpr char kMagic[8] = {'R', 'A', 'S', 'T', 'Y', 'B', 'I', 'N'};
inline constexpr std::uint32_t kVersion = 2;
inline constexpr std::uint32_t kHeaderSize = 128;
inline constexpr std::uint32_t kAlignment = 64;
inline constexpr std::uint32_t kRecordSize = 16;
inline constexpr std::uint32_t kFlagColors = 1u << 0;

// One element of the vertex arrays: the layout of CUDA's float4. w is 1 for positions and colors, 0 for
// normals.
struct alignas(16) VertexRecord {
  float x, y, z, w;
};

// One element of the index array: the layout of CUDA's uint4. pad is always 0.
struct alignas(16) TriangleRecord {
  std::uint32_t v[3];
  std::uint32_t pad;
};

static_assert(sizeof(VertexRecord) == kRecordSize && alignof(VertexRecord) == 16, "a vertex record is 16 bytes");
static_assert(sizeof(TriangleRecord) == kRecordSize && alignof(TriangleRecord) == 16, "a triangle record is 16 bytes");

struct Header {
  char magic[8];                   //   0  "RASTYBIN"
  std::uint32_t version;           //   8  kVersion
  std::uint32_t header_size;       //  12  kHeaderSize
  std::uint32_t flags;             //  16  kFlagColors or 0
  std::uint32_t vertex_count;      //  20  V >= 1
  std::uint32_t triangle_count;    //  24  T >= 1
  std::uint32_t reserved0;         //  28  0
  std::uint64_t positions_offset;  //  32  always 128
  std::uint64_t normals_offset;    //  40
  std::uint64_t colors_offset;     //  48  0 when there are no colors
  std::uint64_t indices_offset;    //  56
  std::uint64_t file_size;         //  64  indices_offset + 16 * T
  Vec3f bounds_min;                //  72
  Vec3f bounds_max;                //  84
  Vec3f center;                    //  96
  float radius;                    // 108
  std::uint8_t reserved1[16];      // 112  0
};

static_assert(sizeof(Header) == kHeaderSize, "the header is 128 bytes");
static_assert(offsetof(Header, positions_offset) == 32 && offsetof(Header, file_size) == 64 &&
                  offsetof(Header, bounds_min) == 72 && offsetof(Header, radius) == 108,
              "header fields must sit at the documented offsets");

}  // namespace bin
}  // namespace rasty
