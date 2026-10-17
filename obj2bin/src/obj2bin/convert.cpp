// convert_model(): picks the reader for a file, then builds the mesh. Also small shared helpers.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <string>

#include "convert.hpp"

namespace rasty::convert {
namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

std::string lowercase_extension(const std::string& path) {
  const std::size_t dot = path.find_last_of('.');
  const std::size_t slash = path.find_last_of("/\\");
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
  std::string ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

}  // namespace

float srgb_to_linear(float c) {
  const double v = std::fmin(std::fmax(static_cast<double>(c), 0.0), 1.0);  // NaN -> 0
  return static_cast<float>(v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4));
}

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw ConvertError("cannot open '" + path + "'");
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  if (size < 0) throw ConvertError("cannot read '" + path + "'");
  in.seekg(0, std::ios::beg);
  std::string data(static_cast<std::size_t>(size), '\0');
  if (size > 0 && !in.read(&data[0], size)) throw ConvertError("error reading '" + path + "'");
  return data;
}

Mesh convert_model(const std::string& path, const ConvertOptions& options, ConvertReport* report) {
  const auto t0 = Clock::now();
  const std::string data = read_file(path);
  const std::string ext = lowercase_extension(path);
  const bool looks_like_ply = data.size() > 3 && data.compare(0, 3, "ply") == 0 && (data[3] == '\n' || data[3] == '\r');
  const bool is_ply = ext == ".ply" || (ext != ".obj" && looks_like_ply);
  if (!is_ply && ext != ".obj") {
    throw ConvertError("'" + path + "': unsupported input (expected an .obj or .ply file)");
  }
  SourceMesh src;
  if (is_ply) {
    src = read_ply(data, path);
  } else {
    const std::size_t slash = path.find_last_of("/\\");
    src = read_obj(data, slash == std::string::npos ? std::string() : path.substr(0, slash + 1), path);
  }
  const double read_seconds = seconds_since(t0);

  const auto t1 = Clock::now();
  BuildStats build;
  Mesh mesh = build_mesh(src, options, &build);
  if (report) {
    report->format = src.format;
    report->input_bytes = data.size();
    report->source_vertices = src.positions.size();
    report->source_faces = src.faces;
    report->source_polygons = src.polygons;
    report->skipped_faces = src.skipped_faces;
    report->source_triangles = src.triangle_count();
    report->build = build;
    report->warnings = std::move(src.warnings);
    report->read_seconds = read_seconds;
    report->build_seconds = seconds_since(t1);
  }
  return mesh;
}

}  // namespace rasty::convert
