cmake_minimum_required(VERSION 3.16)

project(rasty
  DESCRIPTION "3D rasterization engine: the obj2bin asset converter (OBJ/PLY -> .bin)"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RASTY_BUILD_TESTS "Build the GoogleTest unit tests" ON)
option(RASTY_USE_SYSTEM_GTEST "Use an installed GoogleTest instead of downloading it" OFF)

# RASTY_FP_RULES: no fused multiply-add, so the converter writes the same bytes on every compiler.
# RASTY_WARNINGS: for this project's own code only (not passed on to targets that link it).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(RASTY_FP_RULES -ffp-contract=off)
  set(RASTY_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
elseif(MSVC)
  set(RASTY_FP_RULES /fp:precise)
  set(RASTY_WARNINGS /W4 /permissive-)
endif()

# obj2bin: the offline converter. obj2bin_lib holds everything but main(), so the tests can call it.
# include/rasty/bin_format.hpp describes the files it writes.
add_library(obj2bin_lib STATIC
  src/obj2bin/obj_reader.cpp
  src/obj2bin/ply_reader.cpp
  src/obj2bin/build_mesh.cpp
  src/obj2bin/convert.cpp
  src/obj2bin/bin_writer.cpp
  src/obj2bin/cli.cpp)
target_include_directories(obj2bin_lib PUBLIC include src/obj2bin)
target_compile_options(obj2bin_lib PRIVATE ${RASTY_FP_RULES} ${RASTY_WARNINGS})

add_executable(obj2bin src/obj2bin/main.cpp)
target_link_libraries(obj2bin PRIVATE obj2bin_lib)
target_compile_options(obj2bin PRIVATE ${RASTY_WARNINGS})

if(RASTY_BUILD_TESTS)
  # GoogleTest v1.14.0 is downloaded and built with this project's compiler and standard library (a
  # prebuilt copy can be ABI-incompatible, e.g. Anaconda's on macOS). Offline: RASTY_USE_SYSTEM_GTEST=ON,
  # or point FETCHCONTENT_SOURCE_DIR_GOOGLETEST at an unpacked copy.
  if(RASTY_USE_SYSTEM_GTEST)
    find_package(GTest CONFIG REQUIRED)
  else()
    include(FetchContent)
    if(POLICY CMP0135)
      cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  enable_testing()
  add_executable(asset_tests
    tests/test_bin_writer.cpp
    tests/test_obj_reader.cpp
    tests/test_ply_reader.cpp
    tests/test_build_mesh.cpp
    tests/test_cli.cpp)
  target_link_libraries(asset_tests PRIVATE obj2bin_lib GTest::gtest_main)
  target_compile_options(asset_tests PRIVATE ${RASTY_WARNINGS})

  include(GoogleTest)
  gtest_discover_tests(asset_tests DISCOVERY_TIMEOUT 60)
endif()
