// SourceMesh -> Mesh: the step that makes a model ready to render.
//
//   1. Triangles with two corners at the same position are dropped. They have zero area wherever the
//      camera is, so they can never cover a pixel.
//   2. Every corner gets a unit normal: the file's normal if it has a usable one, otherwise the
//      area-weighted average of the normals of all triangles around its vertex (smooth shading). Where
//      those cancel out, the normal of the first triangle around the vertex is used instead.
//   3. Corners with the same file vertex, normal and color become one output vertex, numbered in the
//      order the triangles first use them. Vertices no triangle uses are left out.
//
// The output depends only on the input: sums run in file order in double precision, and the build
// disables fused multiply-add, so every machine writes the same bytes.
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "convert.hpp"

namespace rasty::convert {
namespace {

bool same_bits(const Vec3f& a, const Vec3f& b) { return std::memcmp(&a, &b, sizeof(Vec3f)) == 0; }

bool same_position(const Vec3f& a, const Vec3f& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// v scaled to unit length, computed in double; false if v has no direction (zero, or not finite).
bool normalized(double x, double y, double z, Vec3f& out) {
  const double len = std::sqrt(x * x + y * y + z * z);
  if (!(len > 0.0) || !std::isfinite(len)) return false;
  out = Vec3f{static_cast<float>(x / len), static_cast<float>(y / len), static_cast<float>(z / len)};
  return true;
}

constexpr Vec3f kFallbackNormal{0.0f, 0.0f, 1.0f};

// (p1 - p0) x (p2 - p0) for the triangle whose corners start at c, in double. Its length is twice the
// triangle's area.
std::array<double, 3> face_cross(const SourceMesh& src, const Corner* c) {
  const Vec3f& p0 = src.positions[c[0].position];
  const Vec3f& p1 = src.positions[c[1].position];
  const Vec3f& p2 = src.positions[c[2].position];
  const double ux = static_cast<double>(p1.x) - p0.x, uy = static_cast<double>(p1.y) - p0.y,
               uz = static_cast<double>(p1.z) - p0.z;
  const double vx = static_cast<double>(p2.x) - p0.x, vy = static_cast<double>(p2.y) - p0.y,
               vz = static_cast<double>(p2.z) - p0.z;
  return {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
}

}  // namespace

Mesh build_mesh(const SourceMesh& src, const ConvertOptions& options, BuildStats* stats_out) {
  BuildStats st;
  const std::size_t np = src.positions.size();
  if (src.corners.size() % 3 != 0) throw ConvertError("internal error: corner count is not a multiple of 3");

  // Readers check indices as they read; check again so build_mesh() is safe on any input.
  for (const Corner& c : src.corners) {
    if (c.position >= np || (c.normal != kNone && c.normal >= src.normals.size()) ||
        (c.color != kNone && c.color >= src.colors.size())) {
      throw ConvertError("a triangle corner refers to a vertex, normal or color that does not exist");
    }
  }

  // 1. Triangles that can cover pixels.
  std::vector<std::size_t> kept;
  kept.reserve(src.triangle_count());
  for (std::size_t t = 0; t < src.triangle_count(); ++t) {
    const Corner* c = &src.corners[3 * t];
    const Vec3f& a = src.positions[c[0].position];
    const Vec3f& b = src.positions[c[1].position];
    const Vec3f& d = src.positions[c[2].position];
    if (same_position(a, b) || same_position(b, d) || same_position(a, d)) {
      ++st.degenerate_triangles;
    } else {
      kept.push_back(t);
    }
  }
  if (kept.empty()) {
    throw ConvertError("no triangles left: all " + std::to_string(src.triangle_count()) + " have two corners at the same position");
  }

  // 2. Normals. File normals are normalized; those of zero length are replaced by computed ones. Normals
  // that already have unit length (to float precision) are kept bit for bit, so converting a model
  // exported from a .bin gives back the same .bin.
  const bool use_file_normals = !options.recompute_normals;
  std::vector<Vec3f> file_normal(src.normals.size());
  std::vector<std::uint8_t> file_normal_ok(src.normals.size(), 0);
  if (use_file_normals) {
    for (std::size_t i = 0; i < src.normals.size(); ++i) {
      const Vec3f& n = src.normals[i];
      const double len2 = static_cast<double>(n.x) * n.x + static_cast<double>(n.y) * n.y + static_cast<double>(n.z) * n.z;
      if (std::fabs(len2 - 1.0) <= 1e-6) {
        file_normal[i] = n;
        file_normal_ok[i] = 1;
      } else {
        file_normal_ok[i] = normalized(n.x, n.y, n.z, file_normal[i]) ? 1 : 0;
      }
    }
  }
  const auto uses_file_normal = [&](const Corner& c) {
    return use_file_normals && c.normal != kNone && file_normal_ok[c.normal];
  };

  bool need_computed = false;
  for (const std::size_t t : kept) {
    for (std::size_t k = 0; k < 3; ++k) need_computed = need_computed || !uses_file_normal(src.corners[3 * t + k]);
  }
  std::vector<Vec3f> computed_normal;
  std::vector<std::uint8_t> computed_ok;
  if (need_computed) {
    // Summing the cross products weights each triangle by its area.
    std::vector<std::array<double, 3>> sum(np, std::array<double, 3>{0.0, 0.0, 0.0});
    for (const std::size_t t : kept) {
      const Corner* c = &src.corners[3 * t];
      const std::array<double, 3> n = face_cross(src, c);
      for (std::size_t k = 0; k < 3; ++k) {
        std::array<double, 3>& s = sum[c[k].position];
        s[0] += n[0];
        s[1] += n[1];
        s[2] += n[2];
      }
    }
    computed_normal.assign(np, kFallbackNormal);
    computed_ok.assign(np, 0);
    bool any_undefined = false;
    for (std::size_t p = 0; p < np; ++p) {
      computed_ok[p] = normalized(sum[p][0], sum[p][1], sum[p][2], computed_normal[p]) ? 1 : 0;
      any_undefined = any_undefined || !computed_ok[p];
    }
    // Where the triangle normals cancel out (back-to-back "fin" triangles in scanned models), use the
    // normal of the first triangle around the vertex that has one; it is right for that side at least.
    if (any_undefined) {
      std::vector<std::uint8_t> fixed(np, 0);
      for (const std::size_t t : kept) {
        const Corner* c = &src.corners[3 * t];
        const auto done = [&](std::size_t k) { return computed_ok[c[k].position] || fixed[c[k].position]; };
        if (done(0) && done(1) && done(2)) continue;
        const std::array<double, 3> cross = face_cross(src, c);
        Vec3f n;
        if (!normalized(cross[0], cross[1], cross[2], n)) continue;
        for (std::size_t k = 0; k < 3; ++k) {
          const std::uint32_t p = c[k].position;
          if (computed_ok[p] || fixed[p]) continue;
          computed_normal[p] = n;
          fixed[p] = 1;
        }
      }
    }
  }

  // 3. One output vertex per distinct (file vertex, normal, color), in order of first use. head[p] is
  // the newest output vertex made from file vertex p, next[v] the one made before it.
  const bool with_colors = options.keep_colors && !src.colors.empty();
  Mesh mesh;
  mesh.triangles.reserve(kept.size());
  std::vector<std::uint32_t> head(np, kNone);
  std::vector<std::uint32_t> next;
  for (const std::size_t t : kept) {
    Triangle tri{};
    for (std::size_t k = 0; k < 3; ++k) {
      const Corner& c = src.corners[3 * t + k];
      const bool from_file = uses_file_normal(c);
      const Vec3f normal = from_file ? file_normal[c.normal] : computed_normal[c.position];
      const Vec3f color = !with_colors ? Vec3f{0.0f, 0.0f, 0.0f} : c.color != kNone ? src.colors[c.color] : kDefaultColor;
      std::uint32_t v = head[c.position];
      while (v != kNone && !(same_bits(mesh.normals[v], normal) && (!with_colors || same_bits(mesh.colors[v], color)))) {
        v = next[v];
      }
      if (v == kNone) {
        v = static_cast<std::uint32_t>(mesh.positions.size());
        if (head[c.position] != kNone) ++st.split_vertices;
        mesh.positions.push_back(src.positions[c.position]);
        mesh.normals.push_back(normal);
        if (with_colors) mesh.colors.push_back(color);
        next.push_back(head[c.position]);
        head[c.position] = v;
        if (from_file) {
          ++st.file_normals;
        } else {
          ++st.computed_normals;
          if (!computed_ok[c.position]) ++st.cancelled_normals;
          if (use_file_normals && c.normal != kNone) ++st.invalid_file_normals;
        }
      }
      tri.v[k] = v;
    }
    mesh.triangles.push_back(tri);
  }
  if (mesh.positions.size() >= kNone) throw ConvertError("too many vertices for a .bin file");
  for (std::size_t p = 0; p < np; ++p) st.unused_positions += head[p] == kNone ? 1 : 0;

  mesh.bounds = compute_bounds(mesh.positions);
  if (stats_out) *stats_out = st;
  return mesh;
}

}  // namespace rasty::convert
