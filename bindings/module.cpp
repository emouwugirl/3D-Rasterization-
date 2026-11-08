#include <pybind11/pybind11.h>

#include "obj2bin_bindings.hpp"

int add(int a, int b) {
    return a + b;
}

PYBIND11_MODULE(_native, m) {
    m.doc() = "C++ rendering support for Rasty";
    m.def("add", &add, pybind11::arg("a"), pybind11::arg("b"));
    
    bind_obj2bin(m);
}
