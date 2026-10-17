// Writing .bin files, and the checks a mesh must pass first. The layout is documented in
// rasty/bin_format.hpp.
#include "rasty/bin_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <system_error>

namespace rasty {
namespace {

bool host_is_little_endian() {
  const std::uint16_t one = 1;
  std::uint8_t first = 0;
  std::memcpy(&first, &one, 1);
  return first == 1;
}

std::uint64_t align_up(std::uint64_t n) { return (n + bin::kAlignment - 1) / bin::kAlignment * bin::kAlignment; }

// Where each array goes. The layout is fully determined by V, T and whether there are colors.
struct Layout {
  std::uint64_t positions = 0, normals = 0, colors = 0, indices = 0, file_size = 0;
};

Layout layout_for(std::uint64_t vertices, std::uint64_t triangles, bool has_colors) {
  Layout l;
  l.positions = bin::kHeaderSize;
  l.normals = align_up(l.positions + 12 * vertices);
  std::uint64_t next = align_up(l.normals + 12 * vertices);
  if (has_colors) {
    l.colors = next;
    next = align_up(l.colors + 12 * vertices);
  }
  l.indices = next;
  l.file_size = l.indices + 12 * triangles;
  return l;
}

bool finite(const Vec3f& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

std::string to_string(const Vec3f& v) {
  char buf[96];
  std::snprintf(buf, sizeof buf, "(%.9g, %.9g, %.9g)", static_cast<double>(v.x), static_cast<double>(v.y),
                static_cast<double>(v.z));
  return buf;
}

// RAII wrapper for a C file handle.
struct File {
  std::FILE* f = nullptr;
  File(const std::string& path, const char* mode) : f(std::fopen(path.c_str(), mode)) {}
  ~File() {
    if (f) std::fclose(f);
  }
  File(const File&) = delete;
  File& operator=(const File&) = delete;
};

}  // namespace

Bounds compute_bounds(const std::vector<Vec3f>& positions) {
  Bounds b;
  if (positions.empty()) return b;
  b.min = b.max = positions[0];
  for (const Vec3f& p : positions) {
    b.min = Vec3f{std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z)};
    b.max = Vec3f{std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z)};
  }
  const auto mid = [](float lo, float hi) {
    return static_cast<float>((static_cast<double>(lo) + static_cast<double>(hi)) * 0.5);
  };
  b.center = Vec3f{mid(b.min.x, b.max.x), mid(b.min.y, b.max.y), mid(b.min.z, b.max.z)};
  double r2 = 0.0;
  for (const Vec3f& p : positions) {
    const double dx = static_cast<double>(p.x) - static_cast<double>(b.center.x);
    const double dy = static_cast<double>(p.y) - static_cast<double>(b.center.y);
    const double dz = static_cast<double>(p.z) - static_cast<double>(b.center.z);
    r2 = std::max(r2, dx * dx + dy * dy + dz * dz);
  }
  const double r = std::sqrt(r2);
  b.radius = static_cast<float>(r);
  if (static_cast<double>(b.radius) < r) {  // round up, so that no vertex lies outside the sphere
    b.radius = std::nextafter(b.radius, std::numeric_limits<float>::infinity());
  }
  return b;
}

void validate_mesh(const Mesh& mesh) {
  const std::size_t nv = mesh.positions.size();
  const std::size_t nt = mesh.triangles.size();
  const auto fail = [](const std::string& msg) { throw MeshError(msg); };
  if (nv == 0) fail("the mesh has no vertices");
  if (nt == 0) fail("the mesh has no triangles");
  if (nv > std::numeric_limits<std::uint32_t>::max() || nt > std::numeric_limits<std::uint32_t>::max()) {
    fail("the mesh has more than 2^32 - 1 vertices or triangles");
  }
  if (mesh.normals.size() != nv) {
    fail("the mesh has " + std::to_string(nv) + " vertices but " + std::to_string(mesh.normals.size()) + " normals");
  }
  if (!mesh.colors.empty() && mesh.colors.size() != nv) {
    fail("the mesh has " + std::to_string(nv) + " vertices but " + std::to_string(mesh.colors.size()) + " colors");
  }
  for (std::size_t i = 0; i < nv; ++i) {
    if (!finite(mesh.positions[i])) fail("position " + std::to_string(i) + " is not finite");
  }
  for (std::size_t i = 0; i < nv; ++i) {
    const Vec3f& n = mesh.normals[i];
    const double len2 = static_cast<double>(n.x) * n.x + static_cast<double>(n.y) * n.y + static_cast<double>(n.z) * n.z;
    if (!finite(n) || !(std::fabs(len2 - 1.0) <= 1e-5)) {
      fail("normal " + std::to_string(i) + " " + to_string(n) + " does not have unit length");
    }
  }
  for (std::size_t i = 0; i < mesh.colors.size(); ++i) {
    const Vec3f& c = mesh.colors[i];
    const bool in_range = c.x >= 0.0f && c.x <= 1.0f && c.y >= 0.0f && c.y <= 1.0f && c.z >= 0.0f && c.z <= 1.0f;
    if (!in_range) fail("color " + std::to_string(i) + " " + to_string(c) + " is outside [0, 1]");
  }
  for (std::size_t t = 0; t < nt; ++t) {
    for (const std::uint32_t v : mesh.triangles[t].v) {
      if (v >= nv) {
        fail("triangle " + std::to_string(t) + " uses vertex " + std::to_string(v) + ", but there are only " +
             std::to_string(nv) + " vertices");
      }
    }
  }
}

void save_mesh(const std::string& path, const Mesh& mesh) {
  validate_mesh(mesh);
  if (!host_is_little_endian()) throw MeshError(".bin files are little-endian, and this machine is not");

  const std::uint64_t nv = mesh.positions.size();
  const std::uint64_t nt = mesh.triangles.size();
  const Layout layout = layout_for(nv, nt, mesh.has_colors());
  const Bounds bounds = compute_bounds(mesh.positions);

  bin::Header h;
  std::memset(&h, 0, sizeof h);
  std::memcpy(h.magic, bin::kMagic, sizeof h.magic);
  h.version = bin::kVersion;
  h.header_size = bin::kHeaderSize;
  h.flags = mesh.has_colors() ? bin::kFlagColors : 0u;
  h.vertex_count = static_cast<std::uint32_t>(nv);
  h.triangle_count = static_cast<std::uint32_t>(nt);
  h.positions_offset = layout.positions;
  h.normals_offset = layout.normals;
  h.colors_offset = layout.colors;
  h.indices_offset = layout.indices;
  h.file_size = layout.file_size;
  h.bounds_min = bounds.min;
  h.bounds_max = bounds.max;
  h.center = bounds.center;
  h.radius = bounds.radius;

  const std::string tmp = path + ".tmp";
  bool ok = true;
  {
    File out(tmp, "wb");
    if (!out.f) throw MeshError("cannot open '" + tmp + "' for writing");
    std::uint64_t written = 0;
    const char zeros[bin::kAlignment] = {};
    const auto put = [&](const void* data, std::uint64_t bytes) {
      if (ok && bytes > 0) ok = std::fwrite(data, 1, static_cast<std::size_t>(bytes), out.f) == bytes;
      written += bytes;
    };
    const auto put_at = [&](std::uint64_t offset, const void* data, std::uint64_t bytes) {
      put(zeros, offset - written);  // padding up to the array's offset (always less than 64 bytes)
      put(data, bytes);
    };
    put(&h, sizeof h);
    put_at(layout.positions, mesh.positions.data(), 12 * nv);
    put_at(layout.normals, mesh.normals.data(), 12 * nv);
    if (mesh.has_colors()) put_at(layout.colors, mesh.colors.data(), 12 * nv);
    put_at(layout.indices, mesh.triangles.data(), 12 * nt);
    ok = ok && written == layout.file_size && std::fflush(out.f) == 0;
  }
  std::error_code ec;
  if (ok) std::filesystem::rename(tmp, path, ec);
  if (!ok || ec) {
    std::filesystem::remove(tmp, ec);
    throw MeshError("error writing '" + path + "'");
  }
}

}  // namespace rasty
