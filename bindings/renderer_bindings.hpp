// declare the bindings for the renderers
// it is just one function that you call from module.cpp

#pragma once

#include <pybind11/pybind11.h>

void bind_renderer(pybind11::module_& module);
