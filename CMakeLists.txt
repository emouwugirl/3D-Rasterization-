cmake_minimum_required(VERSION 3.18)

project(rasty LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)

# Make a direct CMake configure (for example, from VS Code) find the copy of
# pybind11 installed in the selected Python environment. Python packaging builds
# already supply this path, so only discover it when it has not been provided.
# ^^^ chat set this up idk why
if(NOT pybind11_DIR)
  execute_process(
    COMMAND "${Python_EXECUTABLE}" -m pybind11 --cmakedir
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE RASTY_PYBIND11_RESULT)
  if(NOT RASTY_PYBIND11_RESULT EQUAL 0)
    message(FATAL_ERROR "pybind11 is not installed for ${Python_EXECUTABLE}; run 'make setup'")
  endif()
endif()
find_package(pybind11 CONFIG REQUIRED)

enable_testing()
add_subdirectory(obj2bin)

pybind11_add_module(_native 
  bindings/module.cpp
  bindings/obj2bin_bindings.cpp)

target_include_directories(_native PRIVATE bindings)
target_link_libraries(_native PRIVATE obj2bin_lib)

set_target_properties(obj2bin_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# scikit-build-core installs the extension beside src/rasty/__init__.py, making
# it importable as rasty._native on every supported platform.
install(TARGETS _native
  LIBRARY DESTINATION rasty
  RUNTIME DESTINATION rasty)
