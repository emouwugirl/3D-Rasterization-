// cpu_bench: times the CPU renderer on one or more models and prints a table of per-stage times.
//
//   cpu_bench [--width 1920] [--height 1080] [--frames 10] [--threads 0] [--ppm DIR] model.obj|ply ...
//
// Each model is converted in memory (as the GUI does), drawn once to warm up, then drawn --frames times
// with the same orbit camera. Times are medians over those frames. --ppm writes each model's image.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#include "convert.hpp"
#include "cpu_renderer.h"
#include "orbit_camera.h"

namespace {

double median(std::vector<double> v) {
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

std::string base_name(const std::string& path) {
  const std::size_t slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const std::size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

bool write_ppm(const std::string& path, const rasty::FrameBuffer& frame) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%u %u\n255\n", frame.width, frame.height);
  std::vector<unsigned char> row(static_cast<std::size_t>(frame.width) * 3);
  for (std::uint32_t y = 0; y < frame.height; ++y) {
    for (std::uint32_t x = 0; x < frame.width; ++x) {
      const std::uint32_t p = frame.rgba[static_cast<std::size_t>(y) * frame.width + x];
      row[x * 3 + 0] = static_cast<unsigned char>(p & 0xFF);
      row[x * 3 + 1] = static_cast<unsigned char>((p >> 8) & 0xFF);
      row[x * 3 + 2] = static_cast<unsigned char>((p >> 16) & 0xFF);
    }
    std::fwrite(row.data(), 1, row.size(), f);
  }
  return std::fclose(f) == 0;
}

int usage() {
  std::fprintf(stderr,
               "usage: cpu_bench [--width W] [--height H] [--frames N] [--threads T] [--ppm DIR] model...\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::uint32_t width = 1920, height = 1080;
  int frames = 10;
  unsigned threads = 0;
  std::string ppm_dir;
  std::vector<std::string> models;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const bool has_value = i + 1 < argc;
    if (a == "--width" && has_value) width = static_cast<std::uint32_t>(std::atoi(argv[++i]));
    else if (a == "--height" && has_value) height = static_cast<std::uint32_t>(std::atoi(argv[++i]));
    else if (a == "--frames" && has_value) frames = std::max(1, std::atoi(argv[++i]));
    else if (a == "--threads" && has_value) threads = static_cast<unsigned>(std::atoi(argv[++i]));
    else if (a == "--ppm" && has_value) ppm_dir = argv[++i];
    else if (!a.empty() && a[0] == '-') return usage();
    else models.push_back(a);
  }
  if (models.empty() || width == 0 || height == 0) return usage();

  rasty::CpuRendererOptions options;
  options.threads = threads;
  const rasty::CpuRenderer renderer(options);
  std::printf("CPU renderer, %u thread%s, %ux%u, median of %d frames\n\n", renderer.threads(),
              renderer.threads() == 1 ? "" : "s", width, height, frames);
  std::printf("%-22s %10s %9s | %7s %7s %8s %7s | %8s %7s | %9s\n", "model", "triangles", "load s", "clear",
              "vertex", "raster", "shade", "frame ms", "fps", "covered");

  int failures = 0;
  rasty::FrameBuffer frame;
  for (const std::string& path : models) {
    try {
      const auto load_start = std::chrono::steady_clock::now();
      const rasty::Mesh mesh = rasty::convert::convert_model(path, rasty::convert::ConvertOptions{});
      const double load_s =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - load_start).count();
      const rasty::RenderSettings settings =
          rasty::orbit_settings(rasty::compute_bounds(mesh.positions), width, height, rasty::OrbitView{});

      renderer.render_into(mesh, settings, frame);  // warm-up: sizes the buffers, faults in the pages
      std::vector<double> clear, vertex, raster, shade, total;
      for (int f = 0; f < frames; ++f) {
        renderer.render_into(mesh, settings, frame);
        const rasty::CpuFrameStats& s = renderer.last_stats();
        clear.push_back(s.clear_ms);
        vertex.push_back(s.vertex_ms);
        raster.push_back(s.raster_ms);
        shade.push_back(s.shade_ms);
        total.push_back(s.total_ms);
      }
      const rasty::CpuFrameStats& s = renderer.last_stats();
      const double frame_ms = median(total);
      std::printf("%-22s %10llu %9.2f | %7.2f %7.2f %8.2f %7.2f | %8.2f %7.1f | %9llu\n", base_name(path).c_str(),
                  static_cast<unsigned long long>(s.triangles), load_s, median(clear), median(vertex),
                  median(raster), median(shade), frame_ms, 1000.0 / frame_ms,
                  static_cast<unsigned long long>(s.pixels_covered));
      if (!ppm_dir.empty() && !write_ppm(ppm_dir + "/" + base_name(path) + ".ppm", frame)) {
        std::fprintf(stderr, "cannot write %s/%s.ppm\n", ppm_dir.c_str(), base_name(path).c_str());
        ++failures;
      }
    } catch (const std::exception& e) {
      std::fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
      ++failures;
    }
  }
  return failures == 0 ? 0 : 1;
}
