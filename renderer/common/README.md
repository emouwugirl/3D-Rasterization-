# Shared math core

`mathcore.h` is a header-only set of pure math functions for the CPU reference and CUDA renderer. Include
it from either ordinary C++ or a `.cu` file; functions are marked with `RASTY_MATH_HD`, which expands to
CUDA host/device force-inline attributes under nvcc and to `inline` under a regular C++ compiler. The
public API is in `rasty::math`. Types use plain fields and fixed-size arrays, with no STL containers,
allocation, exceptions, recursion, or renderer-specific work.

## Shared conventions

- Right-handed world and view coordinates; +Y is up and the camera looks down -Z.
- Model-space front faces are counter-clockwise. After the viewport flips Y, this library's edge
  formula gives front-facing triangles positive area; `front_facing(area)` tests that convention.
- Matrices are column-major in memory and multiply column vectors. Compose transforms as
  `projection * view * model`.
- `perspective()` maps near to depth 0 and far to depth 1. NDC y is flipped once in `viewport()`, so
  framebuffer row 0 is at the top.
- Pixel samples are at `(x + 0.5, y + 0.5)`. `to_fixed()` snaps screen positions to 8-bit subpixels;
  edge functions and coverage use signed 64-bit integer arithmetic after that point.
- `Vec2f`, `Vec3f`, and `Vec4f` are 16-byte aligned. Their names live in `rasty::math` so they do not
  collide with `rasty::Vec3f`, whose binary layout is defined by the existing `.bin` asset format.

## Pipeline functions

`model_matrix`, `look_at`, and `perspective` construct transforms. Multiply a model-space point by the
combined matrix to get clip coordinates; `perspective_divide` returns NDC and can also return `1/w` for
later interpolation. `all_outside_one_plane` rejects triangles wholly outside a clip plane, and
`clip_near` clips against `z >= 0`, returning a polygon with up to four clip-space vertices for the
caller to triangulate. Side clipping can be skipped when the renderer clamps `bounding_box()` to the
image or tile.

`to_fixed`, `pixel_center`, `edge_function`, `is_top_left`, and `inside_top_left` implement deterministic
coverage. Reject zero-area and back-facing triangles first. For a front-facing triangle, positive
edge values are inside; zero is accepted only on top or left edges. Use the original edge values
for interpolation. `edge_step_x` and `edge_step_y` give the per-pixel increments. `barycentric` and
`barycentric_at_pixel` provide interpolation weights. Depth is linear in screen space, while
`interpolate_perspective` applies perspective correction to 2D, 3D, or 4D attributes.

Finally, `quantize_depth` and `pack_depth_and_id` build an unsigned 64-bit depth/triangle key suitable
for integer minimum comparisons, with lower triangle IDs winning exact depth ties. The second pass can
use `id_from_key`, perspective-correct normal interpolation, `shade_lambert`, and `to_rgba8` to shade
the winning triangle per pixel.

## CMake and CUDA

The shared headers live in `renderer/common/`. The `mathcore` target exports this folder and requires
C++20. `renderer.h` defines the host-side `rasty::Renderer` interface shared by CPU and GPU
implementations: it takes the existing `rasty::Mesh` and `RenderSettings`, then returns a row-major
`FrameBuffer` of packed `0xAABBGGRR` pixels. Its contract uses pixel-center/top-left coverage and the
depth/triangle-ID tie-break described above. The `renderer_api` target exports this interface and
links `mathcore`; both renderer targets link `renderer_api`. They remain interface scaffolds until
implementation sources are added. The root build includes these targets through
`renderer/CMakeLists.txt`.

The math core does not enable CUDA in
the root project; a CUDA renderer can link the target from its own CUDA-enabled build. For parity runs,
compile CUDA with fused multiply-add disabled (`--fmad=false`) and without fast math. Integer raster
operations are intended to match exactly; floating-point transform and lighting comparisons still need
a small tolerance. Test coverage should exercise the same functions in host code and a one-thread CUDA
kernel.

## Outside this library

Triangle/pixel loops, tiling and binning, atomics, mesh loading, framebuffers, window output, and timing
belong to the renderer and surrounding modules. The core provides calculations and small value types;
it does not schedule work or own data buffers.
