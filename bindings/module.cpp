// define the bindings for pybind11
// this is the main file that defines all the bindings
// put the bindings for each module into its own hpp/cpp pair
// then call down there to add them to the bindings

#include <pybind11/pybind11.h>

#include "obj2bin_bindings.hpp"

int add(int a, int b) {
    return a + b;
}

PYBIND11_MODULE(_native, m) {
    m.doc() = "C++ rendering support for Rasty";

    // test
    m.def("add", &add, pybind11::arg("a"), pybind11::arg("b"));
    
    // bind everything for obj2bin
    bind_obj2bin(m);
}
