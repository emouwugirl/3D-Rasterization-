// PLY reader: a Stanford PLY file (ascii, binary_little_endian or binary_big_endian) -> SourceMesh.
//
// Vertex properties read: x y z, nx ny nz, red green blue. The face element's "vertex_indices" (or
// "vertex_index") list gives each face's corners; faces with more than three are fan-triangulated.
// Every other property and element (confidence, intensity, range_grid, ...) is read past and ignored.
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "byte_order.hpp"
#include "convert.hpp"
#include "text_scan.hpp"

namespace rasty::convert {
namespace {

enum class PlyType : std::uint8_t { Int8, UInt8, Int16, UInt16, Int32, UInt32, Float32, Float64 };

bool parse_ply_type(std::string_view s, PlyType& t) {
  if (s == "char" || s == "int8") t = PlyType::Int8;
  else if (s == "uchar" || s == "uint8") t = PlyType::UInt8;
  else if (s == "short" || s == "int16") t = PlyType::Int16;
  else if (s == "ushort" || s == "uint16") t = PlyType::UInt16;
  else if (s == "int" || s == "int32") t = PlyType::Int32;
  else if (s == "uint" || s == "uint32") t = PlyType::UInt32;
  else if (s == "float" || s == "float32") t = PlyType::Float32;
  else if (s == "double" || s == "float64") t = PlyType::Float64;
  else return false;
  return true;
}

std::size_t type_size(PlyType t) {
  switch (t) {
    case PlyType::Int8:
    case PlyType::UInt8: return 1;
    case PlyType::Int16:
    case PlyType::UInt16: return 2;
    case PlyType::Int32:
    case PlyType::UInt32:
    case PlyType::Float32: return 4;
    case PlyType::Float64: return 8;
  }
  return 0;
}

bool is_integer_type(PlyType t) { return t != PlyType::Float32 && t != PlyType::Float64; }

struct PlyProperty {
  std::string name;
  PlyType type = PlyType::Float32;  // the item type, for lists
  bool is_list = false;
  PlyType count_type = PlyType::UInt8;
};

struct PlyElement {
  std::string name;
  std::uint64_t count = 0;
  std::vector<PlyProperty> props;
};

enum class PlyFormat { Ascii, BinaryLittleEndian, BinaryBigEndian };

// Sequential reader for the data after the header. Errors throw ConvertError with a short message; the
// caller adds the file and element.
class PlyBody {
 public:
  PlyBody(std::string_view data, std::size_t offset, PlyFormat format)
      : p_(data.data() + offset),
        end_(data.data() + data.size()),
        format_(format),
        swap_(format != PlyFormat::Ascii && (format == PlyFormat::BinaryLittleEndian) != detail::host_is_little_endian()) {}

  double read(PlyType t) {
    if (format_ == PlyFormat::Ascii) {
      next_token();
      const char* s = tok_;
      if (is_integer_type(t)) {
        std::int64_t v = 0;
        if (!detail::parse_int(s, tok_end_, v) || s != tok_end_) bad_token();
        return static_cast<double>(v);
      }
      double v = 0.0;
      if (!detail::parse_double(s, tok_end_, v) || s != tok_end_) bad_token();
      return v;
    }
    const std::size_t n = type_size(t);
    if (static_cast<std::size_t>(end_ - p_) < n) throw ConvertError("unexpected end of data");
    std::uint8_t b[8];
    std::memcpy(b, p_, n);
    p_ += n;
    if (swap_) {
      for (std::size_t i = 0; i < n / 2; ++i) std::swap(b[i], b[n - 1 - i]);
    }
    switch (t) {
      case PlyType::Int8: { std::int8_t v; std::memcpy(&v, b, 1); return v; }
      case PlyType::UInt8: return b[0];
      case PlyType::Int16: { std::int16_t v; std::memcpy(&v, b, 2); return v; }
      case PlyType::UInt16: { std::uint16_t v; std::memcpy(&v, b, 2); return v; }
      case PlyType::Int32: { std::int32_t v; std::memcpy(&v, b, 4); return v; }
      case PlyType::UInt32: { std::uint32_t v; std::memcpy(&v, b, 4); return v; }
      case PlyType::Float32: { float v; std::memcpy(&v, b, 4); return static_cast<double>(v); }
      case PlyType::Float64: { double v; std::memcpy(&v, b, 8); return v; }
    }
    return 0.0;
  }

  // An integer value (list lengths, vertex indices). Values stored as floats must be whole numbers.
  std::int64_t read_integer(PlyType t) {
    const double v = read(t);
    // Range check first: converting a NaN or out-of-range double to an integer is undefined.
    if (!(v >= -9.0e18 && v <= 9.0e18)) throw ConvertError("expected an integer");
    const auto i = static_cast<std::int64_t>(v);
    if (static_cast<double>(i) != v) throw ConvertError("expected an integer");
    return i;
  }

  // Length of a list of `item` values, checked against the data left so that a corrupt length cannot
  // cause a huge allocation.
  std::int64_t read_list_length(PlyType count_type, PlyType item) {
    const std::int64_t n = read_integer(count_type);
    if (n < 0) throw ConvertError("negative list length");
    const std::uint64_t left = bytes_left();
    const std::uint64_t max_items = format_ == PlyFormat::Ascii ? (left + 1) / 2 : left / type_size(item);
    if (static_cast<std::uint64_t>(n) > max_items) {
      throw ConvertError("a list of " + std::to_string(n) + " values is longer than the rest of the file");
    }
    return n;
  }

  void skip_list_or_value(const PlyProperty& p) {
    if (!p.is_list) {
      read(p.type);
      return;
    }
    const std::int64_t n = read_list_length(p.count_type, p.type);
    for (std::int64_t j = 0; j < n; ++j) read(p.type);
  }

  std::uint64_t bytes_left() const { return static_cast<std::uint64_t>(end_ - p_); }

  void skip_bytes(std::uint64_t n) {
    if (bytes_left() < n) throw ConvertError("unexpected end of data");
    p_ += n;
  }

 private:
  void next_token() {
    while (p_ < end_ && (detail::is_space(*p_) || *p_ == '\n')) ++p_;
    if (p_ >= end_) throw ConvertError("unexpected end of data");
    tok_ = p_;
    while (p_ < end_ && !detail::is_space(*p_) && *p_ != '\n') ++p_;
    tok_end_ = p_;
  }
  [[noreturn]] void bad_token() const {
    throw ConvertError("invalid number '" + std::string(tok_, static_cast<std::size_t>(tok_end_ - tok_)) + "'");
  }

  const char* p_;
  const char* end_;
  const char* tok_ = nullptr;
  const char* tok_end_ = nullptr;
  PlyFormat format_;
  bool swap_;
};

std::vector<std::string_view> split_words(std::string_view line) {
  std::vector<std::string_view> words;
  const char* p = line.data();
  const char* const end = p + line.size();
  for (std::string_view w = detail::next_token(p, end); !w.empty(); w = detail::next_token(p, end)) words.push_back(w);
  return words;
}

// An integer color channel scaled to 0..1 by its type's range; float channels are already 0..1.
float color_channel(double v, PlyType t) {
  switch (t) {
    case PlyType::Int8:
    case PlyType::UInt8: return static_cast<float>(v / 255.0);
    case PlyType::Int16:
    case PlyType::UInt16: return static_cast<float>(v / 65535.0);
    default: return detail::to_float(v);
  }
}

}  // namespace

SourceMesh read_ply(std::string_view data, const std::string& name) {
  const auto fail = [&](const std::string& msg) -> void { throw ConvertError(name + ": " + msg); };

  // ---- Header ----
  std::size_t pos = 0;
  {
    const char* start = data.data();
    detail::skip_utf8_bom(start, data.data() + data.size());
    pos = static_cast<std::size_t>(start - data.data());
  }
  std::size_t line_no = 0;
  const auto next_line = [&](std::string_view& line) {
    if (!detail::next_line(data, pos, line)) return false;
    ++line_no;
    return true;
  };

  SourceMesh out;
  std::string_view line;
  if (!next_line(line) || line != "ply") fail("not a PLY file (it does not start with 'ply')");
  PlyFormat format = PlyFormat::Ascii;
  bool have_format = false;
  bool header_done = false;
  std::vector<PlyElement> elements;
  while (next_line(line)) {
    const std::vector<std::string_view> w = split_words(line);
    if (w.empty() || w[0] == "comment" || w[0] == "obj_info") continue;
    const std::string where = "header line " + std::to_string(line_no) + ": ";
    if (w[0] == "end_header") {
      header_done = true;
      break;
    }
    if (w[0] == "format") {
      if (w.size() < 2) fail(where + "incomplete format line");
      if (w[1] == "ascii") format = PlyFormat::Ascii;
      else if (w[1] == "binary_little_endian") format = PlyFormat::BinaryLittleEndian;
      else if (w[1] == "binary_big_endian") format = PlyFormat::BinaryBigEndian;
      else fail(where + "unknown format '" + std::string(w[1]) + "'");
      have_format = true;
      out.format = "PLY " + std::string(w[1]);
    } else if (w[0] == "element") {
      if (w.size() < 3) fail(where + "incomplete element line");
      PlyElement el;
      el.name = std::string(w[1]);
      const char* s = w[2].data();
      std::int64_t count = 0;
      if (!detail::parse_int(s, w[2].data() + w[2].size(), count) || count < 0) fail(where + "bad element count");
      el.count = static_cast<std::uint64_t>(count);
      elements.push_back(std::move(el));
    } else if (w[0] == "property") {
      if (elements.empty()) fail(where + "property before any element");
      PlyProperty prop;
      if (w.size() >= 5 && w[1] == "list") {
        prop.is_list = true;
        if (!parse_ply_type(w[2], prop.count_type) || !is_integer_type(prop.count_type)) {
          fail(where + "bad list length type '" + std::string(w[2]) + "'");
        }
        if (!parse_ply_type(w[3], prop.type)) fail(where + "bad list item type '" + std::string(w[3]) + "'");
        prop.name = std::string(w[4]);
      } else if (w.size() >= 3 && w[1] != "list") {
        if (!parse_ply_type(w[1], prop.type)) fail(where + "bad property type '" + std::string(w[1]) + "'");
        prop.name = std::string(w[2]);
      } else {
        fail(where + "malformed property line");
      }
      elements.back().props.push_back(std::move(prop));
    } else {
      out.warnings.push_back(name + ": " + where + "ignoring unknown header line");
    }
  }
  if (!header_done) fail("missing end_header");
  if (!have_format) fail("missing format line");

  // Every entry takes a minimum number of bytes. Reject counts the data cannot hold before allocating
  // anything, so a corrupt header cannot cause a huge allocation.
  {
    const std::uint64_t available = static_cast<std::uint64_t>(data.size() - pos) + (format == PlyFormat::Ascii ? 1 : 0);
    std::uint64_t needed = 0;
    for (const PlyElement& el : elements) {
      std::uint64_t row = 0;
      for (const PlyProperty& p : el.props) row += format == PlyFormat::Ascii ? 2 : type_size(p.is_list ? p.count_type : p.type);
      if (row == 0) continue;
      if (el.count > (available - needed) / row) {
        fail("element '" + el.name + "' declares " + std::to_string(el.count) + " entries, more than the file can hold");
      }
      needed += el.count * row;
    }
  }

  const PlyElement* vertex_el = nullptr;
  const PlyElement* face_el = nullptr;
  for (const PlyElement& el : elements) {
    if (el.name == "vertex" && !vertex_el) vertex_el = &el;
    if (el.name == "face" && !face_el) face_el = &el;
  }
  if (!vertex_el) fail("no 'vertex' element");
  if (!face_el) fail("no 'face' element (point clouds and triangle strips are not supported)");
  if (vertex_el->count >= kNone) fail("too many vertices");

  // ---- Vertex property roles ----
  enum Role { kX, kY, kZ, kNX, kNY, kNZ, kR, kG, kB, kNumRoles };
  int role[kNumRoles];
  for (int& r : role) r = -1;
  for (std::size_t k = 0; k < vertex_el->props.size(); ++k) {
    const PlyProperty& p = vertex_el->props[k];
    if (p.is_list) continue;
    const std::string& n = p.name;
    const int i = static_cast<int>(k);
    if (n == "x") role[kX] = i;
    else if (n == "y") role[kY] = i;
    else if (n == "z") role[kZ] = i;
    else if (n == "nx" || n == "normal_x") role[kNX] = i;
    else if (n == "ny" || n == "normal_y") role[kNY] = i;
    else if (n == "nz" || n == "normal_z") role[kNZ] = i;
    else if (n == "red" || n == "r" || n == "diffuse_red") role[kR] = i;
    else if (n == "green" || n == "g" || n == "diffuse_green") role[kG] = i;
    else if (n == "blue" || n == "b" || n == "diffuse_blue") role[kB] = i;
  }
  if (role[kX] < 0 || role[kY] < 0 || role[kZ] < 0) fail("the vertex element lacks x, y or z");
  const bool has_normals = role[kNX] >= 0 && role[kNY] >= 0 && role[kNZ] >= 0;
  const bool has_colors = role[kR] >= 0 && role[kG] >= 0 && role[kB] >= 0;

  int index_prop = -1;
  for (std::size_t k = 0; k < face_el->props.size(); ++k) {
    const PlyProperty& p = face_el->props[k];
    if (p.is_list && (p.name == "vertex_indices" || p.name == "vertex_index")) {
      index_prop = static_cast<int>(k);
      break;
    }
  }
  if (index_prop < 0) fail("the face element has no 'vertex_indices' list");

  // ---- Data ----
  const auto nv = static_cast<std::size_t>(vertex_el->count);
  out.positions.resize(nv);
  if (has_normals) out.normals.resize(nv);
  if (has_colors) out.colors.resize(nv);
  out.corners.reserve(static_cast<std::size_t>(face_el->count) * 3);

  PlyBody body(data, pos, format);
  const PlyElement* current = nullptr;
  std::uint64_t current_index = 0;
  std::uint64_t first_skipped = 0;
  try {
    std::vector<double> vals;
    std::vector<std::uint32_t> poly;
    for (const PlyElement& el : elements) {
      current = &el;
      current_index = 0;
      if (&el == vertex_el) {
        vals.assign(el.props.size(), 0.0);
        const auto get = [&](Role r) { return vals[static_cast<std::size_t>(role[r])]; };
        const auto channel = [&](Role r) {
          return srgb_to_linear(color_channel(get(r), el.props[static_cast<std::size_t>(role[r])].type));
        };
        for (; current_index < el.count; ++current_index) {
          for (std::size_t k = 0; k < el.props.size(); ++k) {
            if (el.props[k].is_list) {
              body.skip_list_or_value(el.props[k]);
            } else {
              vals[k] = body.read(el.props[k].type);
            }
          }
          const auto i = static_cast<std::size_t>(current_index);
          const Vec3f p{detail::to_float(get(kX)), detail::to_float(get(kY)), detail::to_float(get(kZ))};
          if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) throw ConvertError("vertex position is not finite");
          out.positions[i] = p;
          if (has_normals) out.normals[i] = Vec3f{detail::to_float(get(kNX)), detail::to_float(get(kNY)), detail::to_float(get(kNZ))};
          if (has_colors) out.colors[i] = Vec3f{channel(kR), channel(kG), channel(kB)};
        }
      } else if (&el == face_el) {
        for (; current_index < el.count; ++current_index) {
          std::size_t corners = 0;
          for (std::size_t k = 0; k < el.props.size(); ++k) {
            const PlyProperty& p = el.props[k];
            if (static_cast<int>(k) != index_prop) {
              body.skip_list_or_value(p);
              continue;
            }
            const std::int64_t n = body.read_list_length(p.count_type, p.type);
            poly.resize(static_cast<std::size_t>(n));
            for (std::int64_t j = 0; j < n; ++j) {
              const std::int64_t vi = body.read_integer(p.type);
              if (vi < 0 || static_cast<std::uint64_t>(vi) >= vertex_el->count) {
                throw ConvertError("vertex index " + std::to_string(vi) + " is out of range (the file has " +
                                   std::to_string(vertex_el->count) + " vertices)");
              }
              poly[static_cast<std::size_t>(j)] = static_cast<std::uint32_t>(vi);
            }
            corners = static_cast<std::size_t>(n);
          }
          ++out.faces;
          if (corners < 3) {
            if (out.skipped_faces++ == 0) first_skipped = current_index;
            continue;
          }
          if (corners > 3) ++out.polygons;
          for (std::size_t k = 1; k + 1 < corners; ++k) {  // fan: (0, k, k + 1)
            for (const std::uint32_t v : {poly[0], poly[k], poly[k + 1]}) {
              Corner c;
              c.position = v;
              if (has_normals) c.normal = v;
              if (has_colors) c.color = v;
              out.corners.push_back(c);
            }
          }
        }
      } else {
        // An element the converter does not use. When every property has a fixed size it is skipped in
        // one jump.
        if (el.props.empty()) continue;
        bool fixed = format != PlyFormat::Ascii;
        std::uint64_t row_bytes = 0;
        for (const PlyProperty& p : el.props) {
          fixed = fixed && !p.is_list;
          row_bytes += type_size(p.type);
        }
        if (fixed) {
          if (el.count > body.bytes_left() / row_bytes) throw ConvertError("unexpected end of data");
          body.skip_bytes(row_bytes * el.count);
          continue;
        }
        for (; current_index < el.count; ++current_index) {
          for (const PlyProperty& p : el.props) body.skip_list_or_value(p);
        }
      }
    }
  } catch (const ConvertError& e) {
    throw ConvertError(name + ": element '" + (current ? current->name : std::string("?")) + "' #" +
                       std::to_string(current_index) + ": " + e.what());
  }

  if (out.skipped_faces > 0) {
    out.warnings.push_back(name + ": skipped " + std::to_string(out.skipped_faces) +
                           " faces with fewer than 3 corners (the first is face #" + std::to_string(first_skipped) + ")");
  }
  if (out.corners.empty()) fail("no triangles");
  return out;
}

}  // namespace rasty::convert
