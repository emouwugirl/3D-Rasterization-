// OBJ reader: a Wavefront OBJ file, and the MTL files it names, -> SourceMesh.
//
// Read: v (x y z, then optionally w, or r g b vertex colors), vn, f with v, v/vt, v//vn and v/vt/vn
// corners (negative indices count back from the end of the list so far), mtllib, usemtl, and each
// material's Kd. vt indices are checked, but texture coordinates are not kept: the renderers do not use
// textures. Faces with more than three corners are fan-triangulated.
// Skipped: o, g and s silently; lines (l) and points (p) with one warning; any other statement with one
// warning per keyword.
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "convert.hpp"
#include "text_scan.hpp"

namespace rasty::convert {
namespace {

using detail::is_space;
using detail::parse_float;
using detail::parse_int;
using detail::skip_spaces;

constexpr std::size_t kMaxWarnings = 20;

// Calls fn(begin, end, line_number) for every line, without its terminator, after a UTF-8 byte order mark.
template <typename Fn>
void for_each_line(std::string_view text, Fn&& fn) {
  const char* p = text.data();
  const char* const end = p + text.size();
  detail::skip_utf8_bom(p, end);
  std::size_t line = 0;
  while (p < end) {
    ++line;
    const char* nl = static_cast<const char*>(std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
    const char* line_end = nl ? nl : end;
    fn(p, line_end, line);
    p = nl ? nl + 1 : end;
  }
}

// The next whitespace-separated word on the line (empty at the end of the line).
std::string_view next_token(const char*& p, const char* end) {
  skip_spaces(p, end);
  const char* s = p;
  while (p < end && !is_space(*p)) ++p;
  return std::string_view(s, static_cast<std::size_t>(p - s));
}

// The rest of the line without surrounding whitespace (material and file names may contain spaces).
std::string rest_of_line(const char* p, const char* end) {
  skip_spaces(p, end);
  while (end > p && is_space(end[-1])) --end;
  return std::string(p, static_cast<std::size_t>(end - p));
}

float clamp01(float v) { return std::fmin(std::fmax(v, 0.0f), 1.0f); }  // NaN -> 0

struct Material {
  Vec3f diffuse = kDefaultColor;
  bool defined = false;         // some MTL file defines it
  std::uint32_t color = kNone;  // its entry in SourceMesh::colors, once a face uses it
};

// The largest index a statement refers to, and the line it is on. Positive OBJ indices may refer to
// data defined later in the file, so they are checked at the end.
struct MaxRef {
  std::uint64_t count = 0;  // largest 0-based index + 1
  std::size_t line = 0;
};

class ObjReader {
 public:
  ObjReader(std::string_view text, std::string base_dir, std::string name)
      : text_(text), base_dir_(std::move(base_dir)), name_(std::move(name)) {}

  SourceMesh run() {
    out_.format = "OBJ";
    for_each_line(text_, [this](const char* p, const char* end, std::size_t line) { parse_line(p, end, line); });
    finish();
    return std::move(out_);
  }

 private:
  [[noreturn]] void fail(std::size_t line, const std::string& msg) const {
    throw ConvertError(name_ + ":" + std::to_string(line) + ": " + msg);
  }
  [[noreturn]] void fail(const std::string& msg) const { throw ConvertError(name_ + ": " + msg); }

  void warn(const std::string& msg) {
    if (out_.warnings.size() < kMaxWarnings) {
      out_.warnings.push_back(msg);
    } else {
      ++suppressed_warnings_;
    }
  }

  // OBJ index (1-based, or negative: relative to the end of the list so far) -> 0-based index.
  std::uint32_t resolve(std::int64_t idx, std::size_t count, std::size_t line, const char* what, MaxRef& ref) const {
    std::int64_t r = 0;
    if (idx > 0) {
      r = idx - 1;
    } else if (idx < 0) {
      r = static_cast<std::int64_t>(count) + idx;
      if (r < 0) fail(line, std::string("relative ") + what + " index " + std::to_string(idx) + " points before the first " + what);
    } else {
      fail(line, std::string(what) + " index 0 is invalid (OBJ indices start at 1)");
    }
    if (r >= static_cast<std::int64_t>(kNone)) fail(line, std::string(what) + " index " + std::to_string(idx) + " is too large");
    if (static_cast<std::uint64_t>(r) + 1 > ref.count) ref = MaxRef{static_cast<std::uint64_t>(r) + 1, line};
    return static_cast<std::uint32_t>(r);
  }

  void parse_line(const char* p, const char* end, std::size_t line) {
    skip_spaces(p, end);
    if (p >= end || *p == '#') return;
    const std::string_view kw = next_token(p, end);
    if (kw == "v") {
      parse_vertex(p, end, line);
    } else if (kw == "vn") {
      float n[3];
      for (float& c : n) {
        skip_spaces(p, end);
        if (!parse_float(p, end, c)) fail(line, "expected x y z after 'vn'");
      }
      out_.normals.push_back(Vec3f{n[0], n[1], n[2]});
    } else if (kw == "vt") {
      ++texcoord_count_;
    } else if (kw == "f") {
      parse_face(p, end, line);
    } else if (kw == "usemtl") {
      const std::string name = rest_of_line(p, end);
      current_material_ = name.empty() ? kNone : material_id(name);
    } else if (kw == "mtllib") {
      load_material_libraries(rest_of_line(p, end));
    } else if (kw == "o" || kw == "g" || kw == "s" || kw == "mg") {
      // Objects, groups and smoothing groups do not change what is drawn.
    } else if (kw == "l" || kw == "p") {
      ++lines_and_points_;
    } else if (unknown_keywords_.insert(std::string(kw)).second) {
      warn(name_ + ":" + std::to_string(line) + ": ignoring unsupported statement '" + std::string(kw) + "'");
    }
  }

  // v x y z [w] or v x y z r g b [a]
  void parse_vertex(const char* p, const char* end, std::size_t line) {
    float v[7];
    int n = 0;
    while (true) {
      skip_spaces(p, end);
      if (p >= end || *p == '#') break;
      if (n == 7) fail(line, "too many values after 'v'");
      if (!parse_float(p, end, v[n])) fail(line, "invalid number after 'v'");
      ++n;
    }
    if (n < 3) fail(line, "expected x y z after 'v'");
    if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) fail(line, "vertex position is not finite");
    out_.positions.push_back(Vec3f{v[0], v[1], v[2]});
    if (n >= 6) {
      vertex_color_.resize(out_.positions.size() - 1, kNone);
      vertex_color_.push_back(static_cast<std::uint32_t>(out_.colors.size()));
      out_.colors.push_back(Vec3f{srgb_to_linear(v[3]), srgb_to_linear(v[4]), srgb_to_linear(v[5])});
    } else if (n == 4 && v[3] != 1.0f) {
      ++weighted_vertices_;
    } else if (n == 5) {
      fail(line, "a 'v' statement has 3, 4, 6 or 7 values, not 5");
    }
  }

  void parse_face(const char* p, const char* end, std::size_t line) {
    face_.clear();
    skip_spaces(p, end);
    while (p < end && *p != '#') {
      std::int64_t v = 0, vt = 0, vn = 0;
      bool has_vt = false, has_vn = false;
      if (!parse_int(p, end, v)) fail(line, "malformed face corner");
      if (p < end && *p == '/') {
        ++p;
        if (p < end && *p != '/' && !is_space(*p)) {
          if (!parse_int(p, end, vt)) fail(line, "malformed texture coordinate index");
          has_vt = true;
        }
        if (p < end && *p == '/') {
          ++p;
          if (p < end && !is_space(*p)) {
            if (!parse_int(p, end, vn)) fail(line, "malformed normal index");
            has_vn = true;
          }
        }
      }
      if (p < end && !is_space(*p) && *p != '#') fail(line, "malformed face corner");
      Corner c;
      c.position = resolve(v, out_.positions.size(), line, "vertex", position_ref_);
      if (has_vt) resolve(vt, texcoord_count_, line, "texture coordinate", texcoord_ref_);
      if (has_vn) c.normal = resolve(vn, out_.normals.size(), line, "normal", normal_ref_);
      face_.push_back(c);
      skip_spaces(p, end);
    }
    ++out_.faces;
    if (face_.size() < 3) {
      if (out_.skipped_faces++ == 0) first_skipped_line_ = line;
      return;
    }
    if (face_.size() > 3) ++out_.polygons;
    for (std::size_t k = 1; k + 1 < face_.size(); ++k) {  // fan: (0, k, k + 1)
      out_.corners.push_back(face_[0]);
      out_.corners.push_back(face_[k]);
      out_.corners.push_back(face_[k + 1]);
      triangle_material_.push_back(current_material_);
    }
  }

  std::uint32_t material_id(const std::string& name) {
    const auto it = material_ids_.find(name);
    if (it != material_ids_.end()) return it->second;
    const auto id = static_cast<std::uint32_t>(materials_.size());
    materials_.emplace_back();
    material_names_.push_back(name);
    material_ids_.emplace(name, id);
    return id;
  }

  void load_material_libraries(const std::string& spec) {
    // Normally a list of file names; the whole remainder is tried first so that names with spaces work.
    std::vector<std::string> files;
    std::error_code ec;
    if (!spec.empty() && std::filesystem::is_regular_file(base_dir_ + spec, ec)) {
      files.push_back(spec);
    } else {
      const char* p = spec.data();
      const char* end = p + spec.size();
      for (std::string_view tok = next_token(p, end); !tok.empty(); tok = next_token(p, end)) files.emplace_back(tok);
    }
    for (const std::string& f : files) {
      if (loaded_libraries_.insert(f).second) parse_mtl(base_dir_ + f);
    }
  }

  void parse_mtl(const std::string& path) {
    std::string text;
    try {
      text = read_file(path);
    } catch (const ConvertError&) {
      warn(name_ + ": cannot read material library '" + path + "'; its materials get the default color");
      return;
    }
    std::uint32_t current = kNone;
    for_each_line(text, [&](const char* p, const char* end, std::size_t line) {
      skip_spaces(p, end);
      if (p >= end || *p == '#') return;
      const std::string_view kw = next_token(p, end);
      if (kw == "newmtl") {
        current = material_id(rest_of_line(p, end));
        materials_[current].defined = true;
      } else if (kw == "Kd" && current != kNone) {
        float c[3];
        skip_spaces(p, end);
        if (!parse_float(p, end, c[0])) {
          warn(path + ":" + std::to_string(line) + ": unsupported Kd form ignored");
          return;
        }
        skip_spaces(p, end);
        if (!parse_float(p, end, c[1])) {
          c[1] = c[2] = c[0];  // "Kd r" means gray
        } else {
          skip_spaces(p, end);
          if (!parse_float(p, end, c[2])) {
            warn(path + ":" + std::to_string(line) + ": Kd needs one or three values");
            return;
          }
        }
        materials_[current].diffuse = Vec3f{clamp01(c[0]), clamp01(c[1]), clamp01(c[2])};
      } else if (kw == "map_Kd") {
        uses_textures_ = true;
      }
    });
  }

  void finish() {
    const auto check = [&](const MaxRef& ref, std::size_t count, const char* what) {
      if (ref.count > count) {
        fail(ref.line, std::string("a face uses ") + what + " " + std::to_string(ref.count) + ", but the file defines " +
                           std::to_string(count));
      }
    };
    check(position_ref_, out_.positions.size(), "vertex");
    check(normal_ref_, out_.normals.size(), "normal");
    check(texcoord_ref_, texcoord_count_, "texture coordinate");
    if (out_.corners.empty()) fail("no triangles (the file has no faces with three or more corners)");

    if (out_.skipped_faces > 0) {
      warn(name_ + ": skipped " + std::to_string(out_.skipped_faces) + " faces with fewer than 3 corners (the first on line " +
           std::to_string(first_skipped_line_) + ")");
    }
    if (lines_and_points_ > 0) {
      warn(name_ + ": ignored " + std::to_string(lines_and_points_) + " line and point statements (only faces are drawn)");
    }
    if (weighted_vertices_ > 0) {
      warn(name_ + ": ignored the w coordinate of " + std::to_string(weighted_vertices_) + " vertices");
    }
    if (uses_textures_) warn(name_ + ": textures (map_Kd) are not supported; materials keep only their Kd color");
    assign_colors();
    if (suppressed_warnings_ > 0) out_.warnings.push_back("(" + std::to_string(suppressed_warnings_) + " more warnings)");
  }

  // Corner colors: the vertex color if the vertex has one, else the face's material color, else the
  // default color. A model with neither vertex colors nor defined materials gets no colors at all.
  void assign_colors() {
    std::vector<std::string> undefined;
    for (std::size_t m = 0; m < materials_.size(); ++m) {
      if (!materials_[m].defined) undefined.push_back("'" + material_names_[m] + "'");
    }
    if (!undefined.empty()) {
      std::string list;
      for (std::size_t i = 0; i < undefined.size() && i < 5; ++i) list += (i ? ", " : "") + undefined[i];
      if (undefined.size() > 5) list += ", ...";
      warn(name_ + ": " + std::to_string(undefined.size()) + " materials are used but not defined in any MTL file (" + list +
           "); their faces get the default color");
    }

    bool any_material = false;
    for (const std::uint32_t m : triangle_material_) any_material = any_material || (m != kNone && materials_[m].defined);
    if (out_.colors.empty() && !any_material) return;

    std::size_t colored = 0;
    for (const std::uint32_t c : vertex_color_) colored += c != kNone ? 1 : 0;
    if (colored > 0 && colored < out_.positions.size()) {
      warn(name_ + ": only " + std::to_string(colored) + " of " + std::to_string(out_.positions.size()) +
           " vertices have colors; the others take their face's material color");
    }
    std::uint32_t default_color = kNone;
    for (std::size_t t = 0; t < triangle_material_.size(); ++t) {
      const std::uint32_t m = triangle_material_[t];
      for (std::size_t k = 0; k < 3; ++k) {
        Corner& c = out_.corners[3 * t + k];
        if (c.position < vertex_color_.size() && vertex_color_[c.position] != kNone) {
          c.color = vertex_color_[c.position];
        } else if (m != kNone && materials_[m].defined) {
          if (materials_[m].color == kNone) materials_[m].color = add_color(materials_[m].diffuse);
          c.color = materials_[m].color;
        } else {
          if (default_color == kNone) default_color = add_color(kDefaultColor);
          c.color = default_color;
        }
      }
    }
  }

  std::uint32_t add_color(const Vec3f& c) {
    out_.colors.push_back(c);
    return static_cast<std::uint32_t>(out_.colors.size() - 1);
  }

  std::string_view text_;
  std::string base_dir_;
  std::string name_;
  SourceMesh out_;

  std::vector<std::uint32_t> vertex_color_;      // per position, into out_.colors (shorter if the rest have none)
  std::vector<std::uint32_t> triangle_material_;  // per triangle, into materials_, or kNone
  std::vector<Material> materials_;
  std::vector<std::string> material_names_;
  std::unordered_map<std::string, std::uint32_t> material_ids_;
  std::uint32_t current_material_ = kNone;
  std::unordered_set<std::string> loaded_libraries_;
  bool uses_textures_ = false;

  std::size_t texcoord_count_ = 0;
  MaxRef position_ref_, normal_ref_, texcoord_ref_;
  std::vector<Corner> face_;
  std::size_t first_skipped_line_ = 0;
  std::uint64_t lines_and_points_ = 0;
  std::uint64_t weighted_vertices_ = 0;
  std::unordered_set<std::string> unknown_keywords_;
  std::size_t suppressed_warnings_ = 0;
};

}  // namespace

SourceMesh read_obj(std::string_view text, const std::string& base_dir, const std::string& name) {
  std::string dir = base_dir;
  if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir.push_back('/');
  return ObjReader(text, std::move(dir), name).run();
}

}  // namespace rasty::convert
