# The CPU renderer: the shared pipeline.h stages run in plain loops, spread over a small thread pool.
add_library(cpu_renderer STATIC
  cpu_renderer.cpp
  thread_pool.cpp)
target_include_directories(cpu_renderer PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/obj2bin/include)  # rasty::Mesh, the type render() takes
find_package(Threads REQUIRED)
target_link_libraries(cpu_renderer PUBLIC renderer_api Threads::Threads)
set_target_properties(cpu_renderer PROPERTIES POSITION_INDEPENDENT_CODE ON)  # linked into _native

# No fused multiply-add, so the CPU computes the same floats the GPU build will with --fmad=false.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(CPU_RENDERER_FP_RULES -ffp-contract=off)
  set(CPU_RENDERER_WARNINGS -Wall -Wextra -Wpedantic -Wshadow)
elseif(MSVC)
  set(CPU_RENDERER_FP_RULES /fp:precise)
  set(CPU_RENDERER_WARNINGS /W4)
endif()
target_compile_options(cpu_renderer PRIVATE ${CPU_RENDERER_FP_RULES} ${CPU_RENDERER_WARNINGS})

# Tests and the benchmark, built with obj2bin's RASTY_BUILD_TESTS switch (pip builds turn it off).
if(RASTY_BUILD_TESTS)
  add_executable(cpu_renderer_tests tests/test_cpu_renderer.cpp)
  target_link_libraries(cpu_renderer_tests PRIVATE cpu_renderer obj2bin_lib GTest::gtest_main)  # compute_bounds
  target_compile_options(cpu_renderer_tests PRIVATE ${CPU_RENDERER_FP_RULES} ${CPU_RENDERER_WARNINGS})
  include(GoogleTest)
  gtest_discover_tests(cpu_renderer_tests DISCOVERY_TIMEOUT 60)

  add_executable(cpu_bench bench/cpu_bench.cpp)
  target_link_libraries(cpu_bench PRIVATE cpu_renderer obj2bin_lib)
  target_compile_options(cpu_bench PRIVATE ${CPU_RENDERER_FP_RULES} ${CPU_RENDERER_WARNINGS})
endif()
