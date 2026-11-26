# CPU renderer

`CpuRenderer` implements `rasty::Renderer` from `renderer/common/renderer.h`. It runs the shared stages
in `renderer/common/pipeline.h` as four plain loops:

| Stage  | Loop           | What it does                                                    |
| ------ | -------------- | --------------------------------------------------------------- |
| clear  | every pixel    | sets the pixel's key to "no triangle"                           |
| vertex | every vertex   | `vertex_stage()` and `world_normal()`                           |
| raster | every triangle | `setup_triangle()`, then `raster_triangle()` right away         |
| shade  | every pixel    | `shade_sample()` for the triangle that won the pixel            |

Raster only keeps one 64-bit key per pixel, `depth << 32 | triangle id`, and keeps the smallest one.
Shading happens afterwards, once per pixel, for the winning triangle.

Each loop is split into pieces that a small thread pool (`thread_pool.h`) hands out to threads.
Two threads can draw over the same pixel, so the depth test is an atomic minimum (`DepthTest`, the CPU
version of CUDA's `atomicMin`). The smallest key wins in any order, so the image is the same for any
number of threads (`SameImageForAnyThreadCount` checks this).

Triangles that reach behind the camera, or more than 8192 pixels off screen, are clipped first
(`clip_triangle()`). Shading gets its weights from the clip-space corners (`clip_barycentric()`), so
clipped triangles shade correctly too.

## Edge conventions

`pipeline.h` uses mathcore's `edge_function()`, `bounding_box()` and the other math, but keeps its own
small edge helpers (`step_right`, `step_down`, `is_top_or_left`). With `edge_function()`, a front-facing
(counter-clockwise) triangle has positive area after the viewport flips y, and pixels inside it have
positive edge values.

On `main`, mathcore's `front_facing()` and `inside_top_left()` assume the opposite sign, and
`inside_top_left()` lets two triangles that share an edge both draw the pixels on it. Joy's `joy` branch
fixes both and also changes `edge_step_x/y` to return whole-pixel steps. Because `pipeline.h` doesn't
call those functions, it renders the same with either version of `mathcore.h` (the tests pass with
both).

## Build, test, benchmark

The tests and `cpu_bench` build when `RASTY_BUILD_TESTS` is on (pip builds turn it off):

```
cmake -S . -B build/cpu-release -DCMAKE_BUILD_TYPE=Release -DRASTY_BUILD_TESTS=ON \
      -DPython_EXECUTABLE=.venv/bin/python -Dpybind11_DIR=$(.venv/bin/python -m pybind11 --cmakedir)
cmake --build build/cpu-release -j
build/cpu-release/renderer/cpu_renderer/cpu_renderer_tests
build/cpu-release/renderer/cpu_renderer/cpu_bench --frames 10 model.obj other.ply
```

`cpu_bench` options: `--width` and `--height` (default 1920 x 1080), `--frames`, `--threads` (0 = all
cores), and `--ppm DIR` to save each image. Times are medians.

## From Python

```python
from rasty import _native
renderer = _native.CpuRenderer(threads=0)  # 0 = one thread per core
renderer.load("teapot.obj")                # OBJ or PLY, converted in memory by obj2bin
frame, stats = renderer.render(900, 600, yaw=0.6, pitch=0.25, distance=3.0)
# frame: numpy uint8 array (600, 900, 4) in RGBA order
# stats: clear_ms, vertex_ms, raster_ms, shade_ms, total_ms and triangle/pixel counts
```

The orbit camera (`orbit_camera.h`) scales every model into the unit sphere, so the same yaw, pitch
and distance frame every model the same way.
