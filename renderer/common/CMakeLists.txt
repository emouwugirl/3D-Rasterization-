add_library(mathcore INTERFACE)
target_include_directories(mathcore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mathcore INTERFACE cxx_std_20)

add_library(renderer_api INTERFACE)
target_include_directories(renderer_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(renderer_api INTERFACE cxx_std_20)
target_link_libraries(renderer_api INTERFACE mathcore)
