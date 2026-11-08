cmake_minimum_required(VERSION 3.18)
project(rasty_mathcore LANGUAGES CXX)

add_library(mathcore INTERFACE)
target_include_directories(mathcore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mathcore INTERFACE cxx_std_20)
