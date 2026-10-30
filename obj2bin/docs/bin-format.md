# The `.bin` mesh format (version 2)

`obj2bin` writes one `.bin` file per model. A `.bin` file holds the mesh in exactly the form the
renderers draw it. Loading one is a few reads straight into arrays, with no parsing and no per-vertex
work, and the arrays can be copied to the GPU as they are.

## What a `.bin` file contains

An **indexed triangle list**. Vertices are stored once, and each triangle is three indices into them.
Every element is a 16-byte record (see [Why 16-byte records](#why-16-byte-records)):

| Array       | Count | Record (16 bytes)                | Meaning |
|-------------|-------|----------------------------------|---------|
| `positions` | V     | `float32` x, y, z, **1**         | Object-space position, unchanged from the source model |
| `normals`   | V     | `float32` x, y, z, **0**         | Unit-length vertex normal, for smooth shading |
| `colors`    | V     | `float32` r, g, b, **1** (optional) | Linear RGB albedo in [0, 1]. Present only if the model has vertex or material colors |
| `indices`   | T     | `uint32` a, b, c, **0**          | One triangle: three vertex indices, counter-clockwise seen from the front |

There's also a header with the counts, where each array starts, and the model's **bounds**: the
axis-aligned box, its center, and a radius around that center that contains every vertex. The frame
loop can place the camera from the header alone.

Triangle `i` is `indices[i]`. Its corners are `positions[indices[i][k]]`, `normals[...]` and `colors[...]`
for k = 0, 1, 2.
Triangles are in the source file's order, so any "first triangle wins" tie rule in the renderers stays
tied to the model file.

### Why shared vertices instead of one record per triangle

A file that stored all three corners of every triangle ("triangle soup") would also work, but for
rendering it is worse in every way that matters. In a closed mesh each vertex is shared by about six
triangles. For the Happy Buddha: 543,652 vertices, 1,087,474 triangles.

- **Vertex work.** A renderer transforms each vertex once, then assembles triangles from the
  transformed vertices. With shared vertices that's 0.54 M transforms per frame. A soup stores 3.26 M
  corners, so it needs 6× the vertex-stage work on both the CPU and the GPU.
- **Size and transfer.** The indexed Buddha is 34.8 MB: 32 bytes per vertex plus 16 bytes per
  triangle. As a soup with normals it would be 104 MB (96 bytes per triangle): 3× more to read from
  disk and to copy to the GPU.
- **Watertight edges.** Two triangles that share an edge reference the *same* two vertices, so both
  renderers rasterize them from bit-identical screen coordinates. That is what makes a fill rule leave
  no gaps or double-drawn pixels along shared edges. With a soup, this would depend on every copy of a
  vertex being transformed identically.
- **Nothing per-triangle could be precomputed anyway.** What a rasterizer needs per triangle (screen
  position, bounding box, edge equations, facing) depends on the camera, so it is computed every
  frame. The one camera-independent value, the face normal, is not needed: facing comes from the
  screen-space winding, and shading uses vertex normals.

You still get one record per triangle: the index array, in draw order. A GPU kernel that runs one thread
per triangle reads `indices[i]` and fetches three vertices.

### Why 16-byte records

Each element is padded from 12 to 16 bytes so that the arrays suit parallel readers:

- **One load per element.** A 16-byte record is exactly a CUDA `float4` or `uint4`, an SSE or NEON
  register, so a GPU thread or a SIMD lane fetches a vertex or a triangle with a single aligned
  128-bit load instead of three 32-bit ones.
- **Coalesced, never split.** Records sit on 16-byte boundaries (every array starts on a 64-byte one),
  so a warp's 32 consecutive loads cover whole cache lines, and a record fetched by index (a vertex
  gathered for a triangle) never straddles two lines. With 12-byte elements, 1 in 4 does.
- **The fourth value is useful.** Positions carry w = 1 and normals w = 0, so one 4×4 matrix multiply
  transforms both correctly (points move with the translation, directions don't), with no special
  case. Colors carry alpha = 1. Triangles carry 0, reserved.

The cost is size: a third more bytes (34.8 MB instead of 26.1 MB for the Buddha), which is read once
at load time.

## What the converter guarantees

Every file `obj2bin` writes has these properties:

- **Valid.** Every index is less than V, all values are finite, normals have unit length, colors lie in
  [0, 1], and the header's bounds match the positions. The converter checks this before it writes.
- **Triangles only.** Polygons are split into a fan `(0, k, k+1)`.
- **One index per corner.** OBJ's separate `v/vt/vn` indices are merged: corners with the same source
  vertex, normal and color share one vertex, and only where those differ is a vertex duplicated.
- **A unit normal for every vertex.** If the model has normals, they are used: normalized, and kept bit
  for bit when they already have unit length. Otherwise each vertex gets the average of the normals of
  the triangles around it, each weighted by the triangle's angle at that vertex. Angle weights add up
  the same however a polygon was split into triangles, so a quad model's normals do not depend on which
  diagonal the split used (area weights would tilt them). Where the normals cancel out (back-to-back
  "fin" triangles in scanned models), the vertex gets the first such triangle's normal; a vertex whose
  triangles all have zero area gets +Z.
- **No triangles with two corners at the same position.** Such a triangle has zero area from every
  viewpoint, so it is dropped (242 of them in the Happy Buddha). Triangles whose three different
  corners lie on a line are kept: telling them apart exactly would need exact arithmetic, and they
  are harmless.
- **No unused vertices, numbered in order of first use.** Vertex `k` first appears in a triangle no
  earlier than vertex `k-1` does. The numbering depends only on the faces, not on how the source file
  ordered its vertex list. (It does not make vertex fetches more local than a well-ordered source: on
  the Happy Buddha, a triangle's three indices span about the same range as in the PLY's numbering.)
- **Deterministic.** The same input gives the same bytes on every machine and every run: the
  converter uses only basic arithmetic and square roots, which round the same way everywhere (angles
  and sRGB decoding avoid libm functions such as `atan2` and `pow`), and it is built without fused
  multiply-add. An OBJ and a PLY of the same model give identical files.

## Layout

All values are little-endian. Every array starts at a multiple of 64 bytes (a cache line), so a file
read or mapped into aligned memory can be used in place. The padding bytes are zero.

| Offset | Size  | Field              | Value |
|--------|-------|--------------------|-------|
| 0      | 8     | `magic`            | `RASTYBIN` |
| 8      | 4     | `version`          | `2` |
| 12     | 4     | `header_size`      | `128` |
| 16     | 4     | `flags`            | bit 0: colors present |
| 20     | 4     | `vertex_count`     | V ≥ 1 |
| 24     | 4     | `triangle_count`   | T ≥ 1 |
| 28     | 4     | reserved           | 0 |
| 32     | 8     | `positions_offset` | 128 |
| 40     | 8     | `normals_offset`   | `align64(128 + 16V)` |
| 48     | 8     | `colors_offset`    | `align64(normals_offset + 16V)`, or 0 without colors |
| 56     | 8     | `indices_offset`   | `align64(end of the previous array)` |
| 64     | 8     | `file_size`        | `indices_offset + 16T` |
| 72     | 12    | `bounds_min`       | smallest x, y, z of any vertex |
| 84     | 12    | `bounds_max`       | largest x, y, z of any vertex |
| 96     | 12    | `center`           | `(bounds_min + bounds_max) / 2` |
| 108    | 4     | `radius`           | every vertex is within this distance of `center` |
| 112    | 16    | reserved           | 0 |

The same layout is the `rasty::bin::Header` struct in [`include/rasty/bin_format.hpp`](../include/rasty/bin_format.hpp),
and the records are `rasty::bin::VertexRecord` and `rasty::bin::TriangleRecord`.

## What is deliberately left out

- **Texture coordinates and textures.** Neither renderer samples textures, so `vt` data is not
  stored, and faces that refer to missing texture coordinates only produce a warning. OBJ materials
  keep their diffuse color (`Kd`), baked into vertex colors.
- **Per-triangle material IDs.** Baking the material color into the vertices gives the renderers a
  single color path, interpolated like the normal.
- **Precomputed per-triangle data** (face normals, bounding boxes), for the reasons above.
- **Compression or quantization** (16-bit normals, etc.): it would cost decode work and precision in
  the pixel comparison for little gain at these sizes.

## Changing the format

Any change to the layout or the meaning of a field bumps `version`. A reader should reject versions it
does not know, so that an old `.bin` is never silently misread. Regenerate files with `obj2bin`. The
converter runs in well under a second per model.

| Version | Change |
|---------|--------|
| 1       | First format: 12-byte elements (`float32[3]`, `uint32[3]`) |
| 2       | Every element padded to a 16-byte record (x, y, z, w / a, b, c, 0) |
