add_library(mathcore INTERFACE)
target_include_directories(mathcore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mathcore INTERFACE cxx_std_20)
