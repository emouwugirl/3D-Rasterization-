#include "renderer_bindings.hpp"

#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "convert.hpp"
#include "cpu_renderer.h"
#include "orbit_camera.h"

#ifdef RASTY_BUILD_GPU
#include <cuda_runtime_api.h>

#include "gpu_renderer.h"
#endif

namespace py = pybind11;

namespace {

class ViewerModel {
 public:
  py::dict load(const std::string& path) {
    rasty::convert::ConvertReport report;
    rasty::Mesh mesh;
    double seconds = 0.0;
    {
      py::gil_scoped_release release;
      const auto start = std::chrono::steady_clock::now();
      mesh = rasty::convert::convert_model(path, rasty::convert::ConvertOptions{}, &report);
      seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
    bounds_ = rasty::compute_bounds(mesh.positions);
    mesh_ = std::make_shared<const rasty::Mesh>(std::move(mesh));

    py::dict info;
    info["path"] = path;
    info["format"] = report.format;
    info["vertex_count"] = mesh_->vertex_count();
    info["triangle_count"] = mesh_->triangle_count();
    info["has_colors"] = mesh_->has_colors();
    info["load_seconds"] = seconds;
    info["warnings"] = report.warnings;
    return info;
  }

 protected:
  rasty::RenderSettings settings(std::uint32_t width, std::uint32_t height,
                                 float yaw, float pitch, float distance) const {
    if (!mesh_) throw std::runtime_error("load a model before rendering");
    rasty::OrbitView view;
    view.yaw = yaw;
    view.pitch = pitch;
    view.distance = distance;
    return rasty::orbit_settings(bounds_, width, height, view);
  }

  // Keep the current model alive if another Python thread loads a model during rendering.
  std::shared_ptr<const rasty::Mesh> mesh_;

 private:
  rasty::Bounds bounds_;
};

py::array_t<std::uint8_t> rgba_array(const rasty::FrameBuffer& frame) {
  // The packed words are RGBA bytes in memory on little-endian machines.
  py::array_t<std::uint8_t> pixels({static_cast<py::ssize_t>(frame.height),
                                   static_cast<py::ssize_t>(frame.width),
                                   static_cast<py::ssize_t>(4)});
  if (!frame.rgba.empty()) {
    std::memcpy(pixels.mutable_data(), frame.rgba.data(), frame.rgba.size() * sizeof(std::uint32_t));
  }
  return pixels;
}

class CpuViewer : public ViewerModel {
 public:
  explicit CpuViewer(unsigned threads) {
    rasty::CpuRendererOptions options;
    options.threads = threads;
    renderer_ = std::make_unique<rasty::CpuRenderer>(options);
  }

  unsigned threads() const { return renderer_->threads(); }

  py::tuple render(std::uint32_t width, std::uint32_t height, float yaw, float pitch, float distance) {
    const auto render_settings = settings(width, height, yaw, pitch, distance);
    const auto mesh = mesh_;
    // Serialize use of the CPU scratch buffers without waiting while holding the GIL.
    std::unique_lock lock(render_mutex_, std::defer_lock);
    {
      py::gil_scoped_release release;
      lock.lock();
      renderer_->render_into(*mesh, render_settings, frame_);
    }

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
    return py::make_tuple(rgba_array(frame_), stats);
  }

 private:
  std::unique_ptr<rasty::CpuRenderer> renderer_;
  std::mutex render_mutex_;
  rasty::FrameBuffer frame_;
};

#ifdef RASTY_BUILD_GPU
class GpuViewer : public ViewerModel {
 public:
  GpuViewer() {
    int device_count = 0;
    const auto status = cudaGetDeviceCount(&device_count);
    if (status != cudaSuccess) {
      throw std::runtime_error(std::string("CUDA is unavailable: ") + cudaGetErrorString(status));
    }
    if (device_count == 0) throw std::runtime_error("No CUDA-capable GPU was found");
  }

  py::tuple render(std::uint32_t width, std::uint32_t height, float yaw, float pitch, float distance) {
    const auto render_settings = settings(width, height, yaw, pitch, distance);
    const auto mesh = mesh_;
    rasty::FrameBuffer frame;
    double total_ms;
    {
      py::gil_scoped_release release;
      const auto start = std::chrono::steady_clock::now();
      frame = renderer_.render(*mesh, render_settings);
      total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    py::dict stats;
    stats["total_ms"] = total_ms;
    return py::make_tuple(rgba_array(frame), stats);
  }

 private:
  rasty::GpuRenderer renderer_;
};
#endif

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
#ifdef RASTY_BUILD_GPU
    py::class_<GpuViewer>(module, "GpuRenderer")
        .def(py::init<>())
        .def("load", &GpuViewer::load, py::arg("path"))
        .def("render", &GpuViewer::render,
             py::arg("width"), py::arg("height"),
             py::arg("yaw") = 0.6f, py::arg("pitch") = 0.25f, py::arg("distance") = 3.0f);
#endif
}
