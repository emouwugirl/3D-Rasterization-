option(RASTY_BUILD_GPU "Build the CUDA renderer" OFF)
if(NOT RASTY_BUILD_GPU)
  return()
endif()

enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)

add_library(gpu_renderer STATIC gpu_renderer.cpp clear.cu transform.cu raster.cu shade.cu)
target_include_directories(gpu_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../obj2bin/include)
target_link_libraries(gpu_renderer PUBLIC renderer_api PRIVATE CUDA::cudart)
# Match the CPU renderer's separate multiply/add operations.
target_compile_options(gpu_renderer PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--fmad=false>)
set_target_properties(gpu_renderer PROPERTIES
  CUDA_STANDARD 20
  CUDA_STANDARD_REQUIRED ON
  CUDA_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON)
