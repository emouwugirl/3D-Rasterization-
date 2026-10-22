// The obj2bin command line: options, the conversion, and its summary.
#include "cli.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "convert.hpp"
#include "rasty/bin_format.hpp"

namespace rasty::convert {
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

std::string vec(const Vec3f& v) {
  char buf[96];
  std::snprintf(buf, sizeof buf, "(%.6g, %.6g, %.6g)", static_cast<double>(v.x), static_cast<double>(v.y),
                static_cast<double>(v.z));
  return buf;
}

std::string default_output(const std::string& input) {
  std::filesystem::path p(input);
  p.replace_extension(".bin");
  return p.string();
}

void print_summary(std::FILE* out, const std::string& input, const std::string& output, const Mesh& mesh,
                   const Bounds& bounds, const ConvertReport& r, double total, double write_seconds) {
  const BuildStats& b = r.build;
  std::fprintf(out, "input     %s (%s, %s)\n", input.c_str(), r.format.c_str(), megabytes(r.input_bytes).c_str());
  std::fprintf(out, "source    %s vertices, %s faces", grouped(r.source_vertices).c_str(), grouped(r.source_faces).c_str());
  if (r.source_polygons > 0) std::fprintf(out, " (%s polygons split into triangles)", grouped(r.source_polygons).c_str());
  std::fprintf(out, " -> %s triangles\n", grouped(r.source_triangles).c_str());
  std::fprintf(out, "removed   %s degenerate triangles, %s unused vertices", grouped(b.degenerate_triangles).c_str(),
               grouped(b.unused_positions).c_str());
  if (r.skipped_faces > 0) std::fprintf(out, ", %s faces with fewer than 3 corners", grouped(r.skipped_faces).c_str());
  std::fprintf(out, "\n");
  std::fprintf(out, "normals   %s from the file, %s computed (angle-weighted)", grouped(b.file_normals).c_str(),
               grouped(b.computed_normals).c_str());
  if (b.invalid_file_normals > 0) std::fprintf(out, ", %s replacing zero-length file normals", grouped(b.invalid_file_normals).c_str());
  if (b.cancelled_normals > 0) {
    std::fprintf(out, ", %s where the triangle normals cancel out (first triangle's used)", grouped(b.cancelled_normals).c_str());
  }
  if (b.default_normals > 0) {
    std::fprintf(out, ", %s with only zero-area triangles (set to +Z)", grouped(b.default_normals).c_str());
  }
  std::fprintf(out, "\n");
  if (b.split_vertices > 0) {
    std::fprintf(out, "split     %s vertices duplicated for differing normals or colors\n", grouped(b.split_vertices).c_str());
  }
  std::fprintf(out, "colors    %s\n", mesh.has_colors() ? "yes" : "none");
  std::fprintf(out, "vertices  %s (positions, normals%s)\n", grouped(mesh.vertex_count()).c_str(),
               mesh.has_colors() ? ", colors" : "");
  std::fprintf(out, "triangles %s\n", grouped(mesh.triangle_count()).c_str());
  std::fprintf(out, "bounds    min %s  max %s\n", vec(bounds.min).c_str(), vec(bounds.max).c_str());
  std::fprintf(out, "sphere    center %s  radius %.6g\n", vec(bounds.center).c_str(), static_cast<double>(bounds.radius));
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::file_size(output, ec);
  std::fprintf(out, "wrote     %s (%s) in %.2f s (read %.2f s, build %.2f s, write %.2f s)\n", output.c_str(),
               ec ? "?" : megabytes(size).c_str(), total, r.read_seconds, r.build_seconds, write_seconds);
}

int convert(const std::string& input, std::string output, const ConvertOptions& options, bool quiet, std::FILE* out,
            std::FILE* err) {
  if (output.empty()) output = default_output(input);
  std::error_code ec;
  if (std::filesystem::equivalent(input, output, ec)) {
    std::fprintf(err, "error: the output '%s' is the input file; choose another name with -o\n", output.c_str());
    return 1;
  }
  if (std::filesystem::path(output).extension() != ".bin") {
    std::fprintf(err, "warning: the output name '%s' does not end in .bin\n", output.c_str());
  }

  // Warnings are printed whether or not the conversion succeeds: they often explain a failure.
  ConvertReport r;
  const auto print_warnings = [&] {
    for (const std::string& w : r.warnings) std::fprintf(err, "warning: %s\n", w.c_str());
  };
  using Clock = std::chrono::steady_clock;
  const auto t0 = Clock::now();
  try {
    const Mesh mesh = convert_model(input, options, &r);
    const auto t1 = Clock::now();
    const Bounds bounds = save_mesh(output, mesh);
    const double write_seconds = std::chrono::duration<double>(Clock::now() - t1).count();
    const double total = std::chrono::duration<double>(Clock::now() - t0).count();
    print_warnings();
    if (!quiet) print_summary(out, input, output, mesh, bounds, r, total, write_seconds);
    return 0;
  } catch (const std::exception& e) {
    print_warnings();
    std::fprintf(err, "error: %s\n", e.what());
    return 1;
  }
}

}  // namespace

int run_obj2bin(const std::vector<std::string>& args, std::FILE* out, std::FILE* err) {
  ConvertOptions options;
  std::vector<std::string> inputs;
  std::string output;
  bool quiet = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "-h" || a == "--help") {
      std::fputs(kUsage, out);
      return 0;
    } else if (a == "-o" || a == "--output") {
      if (++i >= args.size()) {
        std::fprintf(err, "obj2bin: %s needs a path\n", a.c_str());
        return 2;
      }
      output = args[i];
    } else if (a == "--recompute-normals") {
      options.recompute_normals = true;
    } else if (a == "--no-colors") {
      options.keep_colors = false;
    } else if (a == "-q" || a == "--quiet") {
      quiet = true;
    } else if (a.size() > 1 && a[0] == '-') {
      std::fprintf(err, "obj2bin: unknown option '%s'\n%s", a.c_str(), kUsage);
      return 2;
    } else {
      inputs.push_back(a);
    }
  }
  if (inputs.size() != 1) {
    if (inputs.size() > 1) std::fprintf(err, "obj2bin: give one input file (got %zu)\n", inputs.size());
    std::fputs(kUsage, err);
    return 2;
  }
  return convert(inputs[0], output, options, quiet, out, err);
}

}  // namespace rasty::convert
