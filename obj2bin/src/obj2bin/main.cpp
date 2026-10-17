// obj2bin: converts an OBJ or PLY model into a .bin mesh for the renderers.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "convert.hpp"
#include "rasty/bin_format.hpp"

namespace {

const char* const kUsage =
    "usage: obj2bin [options] INPUT.obj|INPUT.ply\n"
    "\n"
    "Converts a model into a .bin mesh: triangles only, one index per corner, a unit normal for every\n"
    "vertex, ready to load and render. See docs/bin-format.md.\n"
    "\n"
    "options:\n"
    "  -o, --output PATH      where to write the .bin (default: INPUT with its extension changed to .bin)\n"
    "      --recompute-normals  ignore the model's normals and compute smooth ones for every vertex\n"
    "      --no-colors        leave vertex and material colors out of the .bin\n"
    "  -q, --quiet            print only warnings and errors\n"
    "  -h, --help             show this help\n";

// 1234567 -> "1,234,567"
std::string grouped(std::uint64_t n) {
  std::string s = std::to_string(n);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
  return s;
}

std::string megabytes(std::uint64_t bytes) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / 1e6);
  return buf;
}

std::string vec(const rasty::Vec3f& v) {
  char buf[96];
  std::snprintf(buf, sizeof buf, "(%.6g, %.6g, %.6g)", static_cast<double>(v.x), static_cast<double>(v.y),
                static_cast<double>(v.z));
  return buf;
}

void print_mesh_summary(const rasty::Mesh& mesh) {
  std::printf("vertices  %s (positions, normals%s)\n", grouped(mesh.vertex_count()).c_str(),
              mesh.has_colors() ? ", colors" : "");
  std::printf("triangles %s\n", grouped(mesh.triangle_count()).c_str());
  std::printf("bounds    min %s  max %s\n", vec(mesh.bounds.min).c_str(), vec(mesh.bounds.max).c_str());
  std::printf("sphere    center %s  radius %.6g\n", vec(mesh.bounds.center).c_str(), static_cast<double>(mesh.bounds.radius));
}

std::string default_output(const std::string& input) {
  std::filesystem::path p(input);
  p.replace_extension(".bin");
  return p.string();
}

int convert(const std::string& input, std::string output, const rasty::convert::ConvertOptions& options, bool quiet) {
  using rasty::convert::ConvertReport;
  if (output.empty()) output = default_output(input);
  if (std::filesystem::path(output).extension() != ".bin" && !quiet) {
    std::fprintf(stderr, "warning: the output name '%s' does not end in .bin\n", output.c_str());
  }
  const auto t0 = std::chrono::steady_clock::now();
  ConvertReport r;
  const rasty::Mesh mesh = rasty::convert::convert_model(input, options, &r);
  const auto t1 = std::chrono::steady_clock::now();
  rasty::save_mesh(output, mesh);
  const double write_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
  const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  for (const std::string& w : r.warnings) std::fprintf(stderr, "warning: %s\n", w.c_str());
  if (quiet) return 0;

  const rasty::convert::BuildStats& b = r.build;
  std::printf("input     %s (%s, %s)\n", input.c_str(), r.format.c_str(), megabytes(r.input_bytes).c_str());
  std::printf("source    %s vertices, %s faces", grouped(r.source_vertices).c_str(), grouped(r.source_faces).c_str());
  if (r.source_polygons > 0) std::printf(" (%s polygons split into triangles)", grouped(r.source_polygons).c_str());
  std::printf(" -> %s triangles\n", grouped(r.source_triangles).c_str());
  std::printf("removed   %s degenerate triangles, %s unused vertices", grouped(b.degenerate_triangles).c_str(),
              grouped(b.unused_positions).c_str());
  if (r.skipped_faces > 0) std::printf(", %s faces with fewer than 3 corners", grouped(r.skipped_faces).c_str());
  std::printf("\n");
  std::printf("normals   %s from the file, %s computed (area-weighted)", grouped(b.file_normals).c_str(),
              grouped(b.computed_normals).c_str());
  if (b.invalid_file_normals > 0) std::printf(", %s replacing zero-length file normals", grouped(b.invalid_file_normals).c_str());
  if (b.cancelled_normals > 0) {
    std::printf(", %s where the triangle normals cancel out (first triangle's used)", grouped(b.cancelled_normals).c_str());
  }
  std::printf("\n");
  if (b.split_vertices > 0) {
    std::printf("split     %s vertices duplicated for differing normals or colors\n", grouped(b.split_vertices).c_str());
  }
  std::printf("colors    %s\n", mesh.has_colors() ? "yes" : "none");
  print_mesh_summary(mesh);
  std::printf("wrote     %s (%s) in %.2f s (read %.2f s, build %.2f s, write %.2f s)\n", output.c_str(),
              megabytes(std::filesystem::file_size(output)).c_str(), total, r.read_seconds, r.build_seconds, write_seconds);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  rasty::convert::ConvertOptions options;
  std::vector<std::string> inputs;
  std::string output;
  bool quiet = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      std::fputs(kUsage, stdout);
      return 0;
    } else if (a == "-o" || a == "--output") {
      if (++i >= argc) {
        std::fprintf(stderr, "obj2bin: %s needs a path\n", a.c_str());
        return 2;
      }
      output = argv[i];
    } else if (a == "--recompute-normals") {
      options.recompute_normals = true;
    } else if (a == "--no-colors") {
      options.keep_colors = false;
    } else if (a == "-q" || a == "--quiet") {
      quiet = true;
    } else if (a.size() > 1 && a[0] == '-') {
      std::fprintf(stderr, "obj2bin: unknown option '%s'\n%s", a.c_str(), kUsage);
      return 2;
    } else {
      inputs.push_back(a);
    }
  }
  if (inputs.size() != 1) {
    std::fputs(kUsage, stderr);
    return 2;
  }
  try {
    return convert(inputs[0], output, options, quiet);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
