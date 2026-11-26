# obj2bin: asset converter

The offline **asset converter** (`obj2bin`) of the 3D rasterization engine (workstream A). It runs
once per model and turns an OBJ or PLY file into a `.bin` file the renderers can load without
parsing any text.

```
model.obj / model.ply ──► obj2bin ──► model.bin
   (text, run once)       triangulate,    (binary: vertex arrays + triangle indices as
                          merge indices,   16-byte float4/uint4 records, ready to draw)
                          compute normals
```

What a `.bin` file contains, and why: **[docs/bin-format.md](docs/bin-format.md)**.

## Build

Requirements: a C++17 compiler and CMake ≥ 3.16. This folder is a self-contained CMake project; run
these from `obj2bin/` (a top-level CMakeLists can also `add_subdirectory(obj2bin)`).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
build/asset_tests
```

The first configure downloads GoogleTest 1.14. Offline, pass `-DRASTY_USE_SYSTEM_GTEST=ON`,
`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<unpacked googletest>`, or `-DRASTY_BUILD_TESTS=OFF`.

CMake targets:

| Target        | Type       | Contents |
|---------------|------------|----------|
| `obj2bin`     | executable | the converter |
| `obj2bin_lib` | library    | the converter without `main()`: OBJ and PLY readers, mesh building, `.bin` writer |
| `asset_tests` | executable | GoogleTest suite |

## Use

```bash
build/obj2bin path/to/happy_vrip.ply             # writes path/to/happy_vrip.bin
build/obj2bin scene.obj -o out/scene.bin
```

Options: `--recompute-normals` ignores the model's normals and computes smooth ones;
`--no-colors` leaves colors out; `-q` prints only warnings and errors. obj2bin refuses to write over
its input file, and prints its warnings even when a conversion fails. Exit status: 0 on success, 1 if
the conversion failed, 2 for bad usage.

Output for the Stanford Happy Buddha:

```
input     happy_vrip.ply (PLY ascii, 42.6 MB)
source    543,652 vertices, 1,087,716 faces -> 1,087,716 triangles
removed   242 degenerate triangles, 0 unused vertices
normals   0 from the file, 543,652 computed (angle-weighted), 10 where the triangle normals cancel out (first triangle's used)
colors    none
vertices  543,652 (positions, normals)
triangles 1,087,474
bounds    min (-0.0461003, 0.0497571, -0.0473989)  max (0.0352217, 0.24778, 0.0340189)
sphere    center (-0.0054393, 0.148769, -0.00669)  radius 0.105838
wrote     happy_vrip.bin (34.8 MB) in 0.22 s (read 0.07 s, build 0.12 s, write 0.02 s)
```

## Input support

- **OBJ:** `v` (with optional `w` or `r g b` vertex colors), `vn`, `f` with `v`, `v/vt`, `v//vn` and
  `v/vt/vn` corners, negative indices, polygons, `mtllib` / `usemtl` with each material's `Kd`. Lines,
  points, groups and smoothing groups are skipped, and unknown statements produce a warning. Texture
  coordinates are not kept; references to missing ones are only a warning. `mtllib` paths are relative
  to the OBJ file unless absolute.
- **PLY:** ASCII and binary (both byte orders); vertex `x y z`, optional `nx ny nz` and
  `red green blue`; faces as a `vertex_indices` list. Other properties and elements are skipped.

Malformed input is an error naming the file and line (OBJ) or element (PLY), e.g.
`scene.obj:120: a face uses vertex 9, but the file defines 8`.

## Code map

```
include/rasty/bin_format.hpp  the .bin layout (Header, constants), Mesh, and the writer's API
src/obj2bin/convert.hpp       the converter's internal API: SourceMesh, readers, build_mesh()
src/obj2bin/obj_reader.cpp    OBJ + MTL -> SourceMesh
src/obj2bin/ply_reader.cpp    PLY -> SourceMesh
src/obj2bin/build_mesh.cpp    SourceMesh -> Mesh (degenerates, normals, vertex merging and order)
src/obj2bin/bin_writer.cpp    save_mesh(): checks the mesh, writes the .bin
src/obj2bin/convert.cpp       convert_model(): picks the reader, builds the mesh
src/obj2bin/text_scan.hpp     lines, words, and exact, fast, locale-independent number parsing
src/obj2bin/byte_order.hpp    the host's byte order
src/obj2bin/cli.cpp           the command line: options, conversion, summary (run_obj2bin())
src/obj2bin/main.cpp          main(), which calls run_obj2bin()
tests/                        GoogleTest suite: writer, OBJ, PLY, mesh building, command line
docs/bin-format.md            the format specification
```
