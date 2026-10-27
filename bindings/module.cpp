#include <pybind11/pybind11.h>

int add(int a, int b) {
    return a + b;
}

PYBIND11_MODULE(rasty_native, m) {
    m.def("add", &add, pybind11::arg("a"), pybind11::arg("b"));
}
