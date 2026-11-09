// declare the bindings for obj2bin
// it is just one function that you call from module.cpp

#pragma once

#include <pybind11/pybind11.h>

void bind_obj2bin(pybind11::module_& module);