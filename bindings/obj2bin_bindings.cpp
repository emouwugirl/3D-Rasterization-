#include "obj2bin_bindings.hpp"

#include <cstdint>
#include <string>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "convert.hpp"
#include "rasty/bin_format.hpp"

namespace py = pybind11;

void bind_obj2bin(py::module_& module)
{
    py::register_exception<rasty::convert::ConvertError>(module, "ConvertError");

    py::register_exception<rasty::MeshError>(module, "MeshError");

    module.def(
        "convert_model",
        [](const std::string& input, const std::string& output, bool recompute_normals, bool keep_colors) {
            // set up convert options
            rasty::convert::ConvertOptions options;
            options.recompute_normals = recompute_normals;
            options.keep_colors = keep_colors;

            // define report and bounds
            rasty::convert::ConvertReport report;
            rasty::Bounds bounds;

            std::size_t vertex_count = 0;
            std::size_t triangle_count = 0;
            bool has_colors = false;

            {
                // idk what this does but chat said i should do this
                py::gil_scoped_release release;

                const rasty::Mesh mesh =
                    rasty::convert::convert_model(
                        input,
                        options,
                        &report);

                vertex_count = mesh.vertex_count();
                triangle_count = mesh.triangle_count();
                has_colors = mesh.has_colors();

                bounds = rasty::save_mesh(output, mesh);
            }

            py::dict result;
            result["input"] = input;
            result["output"] = output;
            result["format"] = report.format;
            result["vertex_count"] = vertex_count;
            result["triangle_count"] = triangle_count;
            result["has_colors"] = has_colors;
            result["warnings"] = report.warnings;
            result["read_seconds"] = report.read_seconds;
            result["build_seconds"] = report.build_seconds;

            result["bounds_min"] = py::make_tuple(
                bounds.min.x,
                bounds.min.y,
                bounds.min.z);

            result["bounds_max"] = py::make_tuple(
                bounds.max.x,
                bounds.max.y,
                bounds.max.z);

            result["center"] = py::make_tuple(
                bounds.center.x,
                bounds.center.y,
                bounds.center.z);

            result["radius"] = bounds.radius;

            return result;
        },
        py::arg("input"),
        py::arg("output"),
        py::arg("recompute_normals") = false,
        py::arg("keep_colors") = true);
}