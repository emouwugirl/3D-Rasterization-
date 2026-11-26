// this file defines the bindings for the CPU renderer
//
// from python:
//   renderer = _native.CpuRenderer(threads=0)        # 0 = one thread per core
//   info = renderer.load("teapot.obj")               # OBJ or PLY, converted in memory by obj2bin
//   frame, stats = renderer.render(900, 600, yaw=0.6, pitch=0.25, distance=3.0)
//   # frame: numpy uint8 array (height, width, 4) in RGBA order; stats: per-stage times in ms

#include "renderer_bindings.hpp"

#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "convert.hpp"
#include "cpu_renderer.h"
#include "orbit_camera.h"

namespace py = pybind11;

namespace {

// A renderer plus the model it draws and the frame it draws into, so Python only passes the camera.
class CpuViewer {
 public:
  explicit CpuViewer(unsigned threads) {
    rasty::CpuRendererOptions options;
    options.threads = threads;
    renderer_ = std::make_unique<rasty::CpuRenderer>(options);
  }

  unsigned threads() const { return renderer_->threads(); }

  py::dict load(const std::string& path) {
    rasty::convert::ConvertReport report;
    rasty::Mesh mesh;
    double seconds = 0.0;
    {
      py::gil_scoped_release release;  // let the GUI stay responsive while a big model loads
      const auto start = std::chrono::steady_clock::now();
      mesh = rasty::convert::convert_model(path, rasty::convert::ConvertOptions{}, &report);
      seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
    mesh_ = std::move(mesh);
    bounds_ = rasty::compute_bounds(mesh_.positions);
    loaded_ = true;

    py::dict info;
    info["path"] = path;
    info["format"] = report.format;
    info["vertex_count"] = mesh_.vertex_count();
    info["triangle_count"] = mesh_.triangle_count();
    info["has_colors"] = mesh_.has_colors();
    info["load_seconds"] = seconds;
    info["warnings"] = report.warnings;
    return info;
  }

  py::tuple render(std::uint32_t width, std::uint32_t height, float yaw, float pitch, float distance) {
    if (!loaded_) throw std::runtime_error("load a model before rendering");
    rasty::OrbitView view;
    view.yaw = yaw;
    view.pitch = pitch;
    view.distance = distance;
    const rasty::RenderSettings settings = rasty::orbit_settings(bounds_, width, height, view);
    {
      py::gil_scoped_release release;
      renderer_->render_into(mesh_, settings, frame_);
    }

    // 0xAABBGGRR words are R, G, B, A bytes in memory on little-endian machines, i.e. RGBA order.
    py::array_t<std::uint8_t> pixels({static_cast<py::ssize_t>(height), static_cast<py::ssize_t>(width),
                                      static_cast<py::ssize_t>(4)});
    std::memcpy(pixels.mutable_data(), frame_.rgba.data(), frame_.rgba.size() * sizeof(std::uint32_t));

    const rasty::CpuFrameStats& s = renderer_->last_stats();
    py::dict stats;
    stats["clear_ms"] = s.clear_ms;
    stats["vertex_ms"] = s.vertex_ms;
    stats["raster_ms"] = s.raster_ms;
    stats["shade_ms"] = s.shade_ms;
    stats["total_ms"] = s.total_ms;
    stats["triangles"] = s.triangles;
    stats["triangles_rasterized"] = s.triangles_rasterized;
    stats["triangles_clipped"] = s.triangles_clipped;
    stats["pixels_covered"] = s.pixels_covered;
    stats["threads"] = s.threads;
    return py::make_tuple(pixels, stats);
  }

 private:
  std::unique_ptr<rasty::CpuRenderer> renderer_;
  rasty::Mesh mesh_;
  rasty::Bounds bounds_;
  rasty::FrameBuffer frame_;
  bool loaded_ = false;
};

}  // namespace

void bind_renderer(py::module_& module)
{
    py::class_<CpuViewer>(module, "CpuRenderer")
        .def(py::init<unsigned>(), py::arg("threads") = 0)
        .def_property_readonly("threads", &CpuViewer::threads)
        .def("load", &CpuViewer::load, py::arg("path"))
        .def("render", &CpuViewer::render,
             py::arg("width"), py::arg("height"),
             py::arg("yaw") = 0.6f, py::arg("pitch") = 0.25f, py::arg("distance") = 3.0f);
}
