# Interface scaffold until the CPU renderer has implementation sources.
add_library(cpu_renderer INTERFACE)
target_link_libraries(cpu_renderer INTERFACE mathcore)
