# Interface scaffold until the GPU renderer has implementation sources.
# Enable CUDA here when CUDA sources are added, keeping CPU-only builds available.
add_library(gpu_renderer INTERFACE)
target_link_libraries(gpu_renderer INTERFACE mathcore)
