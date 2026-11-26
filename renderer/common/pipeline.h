// The rendering stages shared by the CPU and GPU renderers.
//
// Each function handles one vertex, one triangle or one pixel. The CPU renderer calls them from loops,
// and a CUDA renderer can call the same functions from its kernels, so both draw the same pixels.
//
//   vertex_stage     vertex -> clip space -> fixed-point screen position and depth
//   setup_triangle   triangle -> pixel bounding box and three edge equations
//   raster_triangle  walks the box and keeps the smallest (depth << 32 | id) key for each pixel
//   shade_sample     pixel -> color of the triangle that won it
#pragma once

#include <stdint.h>

#include "mathcore.h"

#if defined(__CUDACC__)
#define RASTY_STAGE_HD __host__ __device__ __forceinline__
#else
#define RASTY_STAGE_HD inline
#endif

namespace rasty::pipeline {

// Use the math types by name so they win over rasty::Vec3f (the 12-byte .bin vector).
using math::IRect;
using math::IVec2;
using math::Mat4f;
using math::Vec3f;
using math::Vec4f;
using namespace rasty::math;

// Geometry more than this many pixels outside the image is clipped off before it is snapped to fixed
// point, so screen coordinates always fit comfortably in 32 bits.
constexpr float kGuardBandPixels = 8192.0f;

// Everything that stays the same for a whole frame.
struct FrameConstants {
  Mat4f mvp;              // projection * view * model
  Mat4f normal_to_world;  // turns model-space normals into world-space normals
  int32_t width;
  int32_t height;
  float guard_x;          // guard band in clip space: |x| <= guard_x * w
  float guard_y;          //                           |y| <= guard_y * w
  Vec3f light_direction;  // unit vector pointing toward the light
  Vec3f clear_color;
  Vec3f albedo;           // surface color when the mesh has no vertex colors
  float ambient;
  float diffuse;
  bool cull_back_faces;
};

RASTY_STAGE_HD FrameConstants make_frame_constants(const Mat4f& model, const Mat4f& view,
                                                   const Mat4f& projection, int32_t width, int32_t height) {
  FrameConstants k{};
  k.mvp = projection * view * model;
  if (!normal_matrix(model, k.normal_to_world)) k.normal_to_world = identity();
  k.width = width;
  k.height = height;
  // screen x = (ndc.x + 1) * width / 2, so 8192 px past either side is |ndc.x| <= 1 + 2 * 8192 / width.
  k.guard_x = 1.0f + 2.0f * kGuardBandPixels / (float)width;
  k.guard_y = 1.0f + 2.0f * kGuardBandPixels / (float)height;
  k.light_direction = vec3(0.0f, 0.0f, 1.0f);
  k.clear_color = vec3(0.0f, 0.0f, 0.0f);
  k.albedo = vec3(0.8f, 0.8f, 0.8f);
  k.ambient = 0.2f;
  k.diffuse = 0.8f;
  k.cull_back_faces = true;
  return k;
}

// ---- 1. Vertices -----------------------------------------------------------------------------------

// One transformed vertex (32 bytes).
struct ScreenVertex {
  Vec4f clip;          // clip-space position (used for clipping and shading)
  IVec2 pos;           // screen position in 24.8 fixed point
  float depth;         // 0 at the near plane, 1 at the far plane
  uint32_t projected;  // 1 if pos and depth are valid (in front of the camera, inside the guard band)
};

RASTY_STAGE_HD bool inside_guard_band(const FrameConstants& k, Vec4f clip) {
  return clip.w > 0.0f && clip.z >= 0.0f && absf(clip.x) <= k.guard_x * clip.w &&
         absf(clip.y) <= k.guard_y * clip.w;
}

RASTY_STAGE_HD void project(const FrameConstants& k, Vec4f clip, IVec2& pos, float& depth) {
  const Vec3f ndc = perspective_divide(clip);
  pos = to_fixed(viewport(ndc, k.width, k.height));
  depth = ndc.z;
}

RASTY_STAGE_HD ScreenVertex vertex_stage(const FrameConstants& k, Vec4f position) {
  ScreenVertex v{};
  v.clip = k.mvp * position;
  v.projected = inside_guard_band(k, v.clip) ? 1u : 0u;
  if (v.projected) project(k, v.clip, v.pos, v.depth);
  return v;
}

RASTY_STAGE_HD Vec3f world_normal(const FrameConstants& k, Vec3f model_normal) {
  return transform_normal(k.normal_to_world, model_normal);
}

// ---- 2. Triangle setup -----------------------------------------------------------------------------

// What raster needs for one triangle. The CPU makes one on the stack per triangle; a GPU version would
// keep an array of them between its setup and raster kernels.
struct TriSetup {
  IRect box;            // pixels to test: x in [x0, x1), y in [y0, y1)
  int64_t edge[3];      // edge values at the first pixel of the box
  int64_t step_x[3];    // added for each pixel to the right
  int64_t step_y[3];    // added for each row down
  int64_t min_edge[3];  // a pixel is covered when edge[i] >= min_edge[i]: 0 on top-left edges, else 1
  int64_t area;         // twice the triangle's area in fixed point (positive)
  float depth[3];       // depth at each corner (corner i is opposite edge i)
  uint32_t id;          // triangle index, stored in the low half of each depth key
};

// Returns false if the triangle can't cover any pixel center: zero area, a culled back face, or a
// bounding box with no pixels in the image.
RASTY_STAGE_HD bool setup_triangle(const FrameConstants& k, IVec2 a, IVec2 b, IVec2 c, float za, float zb,
                                   float zc, uint32_t id, TriSetup& t) {
  int64_t area = edge_function(a, b, c);
  if (area == 0) return false;
  if (area < 0) {  // back face
    if (k.cull_back_faces) return false;
    // Swap two corners so the same triangle is in front-facing order.
    const IVec2 p = b; b = c; c = p;
    const float z = zb; zb = zc; zc = z;
    area = -area;
  }

  t.box = bounding_box(a, b, c, k.width, k.height);
  if (t.box.x0 >= t.box.x1 || t.box.y0 >= t.box.y1) return false;

  // Edge i is the side opposite corner i. Its value at a pixel is corner i's weight times the area.
  const IVec2 from[3] = {b, c, a};
  const IVec2 to[3] = {c, a, b};
  const IVec2 first = pixel_center(t.box.x0, t.box.y0);
  for (int i = 0; i < 3; ++i) {
    t.edge[i] = edge_function(from[i], to[i], first);
    t.step_x[i] = math::edge_step_x(from[i], to[i]);
    t.step_y[i] = math::edge_step_y(from[i], to[i]);
    t.min_edge[i] = math::is_top_left(from[i], to[i]) ? 0 : 1;
  }
  t.area = area;
  t.depth[0] = za;
  t.depth[1] = zb;
  t.depth[2] = zc;
  t.id = id;
  return true;
}

// ---- 3. Raster -------------------------------------------------------------------------------------

RASTY_STAGE_HD bool covered(const TriSetup& t, int64_t e0, int64_t e1, int64_t e2) {
  return e0 >= t.min_edge[0] && e1 >= t.min_edge[1] && e2 >= t.min_edge[2];
}

// Depth is linear across the screen, so the edge values (weights times area) interpolate it directly.
RASTY_STAGE_HD float depth_at(const TriSetup& t, int64_t e0, int64_t e1, int64_t e2) {
  return interpolate_depth(barycentric(e0, e1, e2, t.area), t.depth[0], t.depth[1], t.depth[2]);
}

// Walks the bounding box row by row. Moving one pixel just adds step_x to each edge value.
// For every covered pixel it calls keep_min(pixel_index, key), which stores the key if it is smaller
// than what the pixel already has. The key is (depth << 32 | id), so the smallest key is the nearest
// triangle, and a tie goes to the lower id.
template <class KeepMin>
RASTY_STAGE_HD void raster_triangle(const TriSetup& t, int32_t width, KeepMin keep_min) {
  int64_t row0 = t.edge[0], row1 = t.edge[1], row2 = t.edge[2];
  for (int32_t y = t.box.y0; y < t.box.y1; ++y) {
    int64_t e0 = row0, e1 = row1, e2 = row2;
    for (int32_t x = t.box.x0; x < t.box.x1; ++x) {
      if (covered(t, e0, e1, e2)) {
        const float depth = depth_at(t, e0, e1, e2);
        if (depth <= 1.0f) keep_min((int64_t)y * width + x, pack_depth_and_id(depth, t.id));
      }
      e0 += t.step_x[0];
      e1 += t.step_x[1];
      e2 += t.step_x[2];
    }
    row0 += t.step_y[0];
    row1 += t.step_y[1];
    row2 += t.step_y[2];
  }
}

// The same test for one pixel, computed directly instead of by stepping. Gives exactly the numbers
// raster_triangle gets. Returns the key, or kNoDepthId if the pixel isn't covered.
RASTY_STAGE_HD uint64_t cover_pixel(const TriSetup& t, int32_t x, int32_t y) {
  if (x < t.box.x0 || x >= t.box.x1 || y < t.box.y0 || y >= t.box.y1) return kNoDepthId;
  const int64_t dx = x - t.box.x0, dy = y - t.box.y0;
  const int64_t e0 = t.edge[0] + dx * t.step_x[0] + dy * t.step_y[0];
  const int64_t e1 = t.edge[1] + dx * t.step_x[1] + dy * t.step_y[1];
  const int64_t e2 = t.edge[2] + dx * t.step_x[2] + dy * t.step_y[2];
  if (!covered(t, e0, e1, e2)) return kNoDepthId;
  const float depth = depth_at(t, e0, e1, e2);
  return depth <= 1.0f ? pack_depth_and_id(depth, t.id) : kNoDepthId;
}

// ---- Clipping (only for triangles that reach behind the camera or far off screen) -------------------

struct ClipPolygon {
  Vec4f v[8];  // a triangle clipped by 5 planes has at most 8 corners
  int count;
};

// Distance to clip plane `plane` (inside when >= 0). Plane 0 is the near plane, 1-4 the guard band.
RASTY_STAGE_HD float plane_distance(const FrameConstants& k, Vec4f p, int plane) {
  switch (plane) {
    case 0: return p.z;
    case 1: return k.guard_x * p.w - p.x;
    case 2: return k.guard_x * p.w + p.x;
    case 3: return k.guard_y * p.w - p.y;
    default: return k.guard_y * p.w + p.y;
  }
}

// Cuts the triangle against each plane in turn (Sutherland-Hodgman). Draw the result as a fan:
// (v0, v1, v2), (v0, v2, v3), ...
RASTY_STAGE_HD ClipPolygon clip_triangle(const FrameConstants& k, Vec4f a, Vec4f b, Vec4f c) {
  ClipPolygon poly{};
  poly.v[0] = a;
  poly.v[1] = b;
  poly.v[2] = c;
  poly.count = 3;
  for (int plane = 0; plane < 5 && poly.count > 0; ++plane) {
    ClipPolygon out{};
    for (int i = 0; i < poly.count; ++i) {
      const Vec4f s = poly.v[i];
      const Vec4f e = poly.v[(i + 1) % poly.count];
      const float ds = plane_distance(k, s, plane);
      const float de = plane_distance(k, e, plane);
      if (ds >= 0.0f) out.v[out.count++] = s;                     // keep corners on the inside
      if ((ds >= 0.0f) != (de >= 0.0f)) {                         // edge crosses the plane:
        out.v[out.count++] = s + (e - s) * (ds / (ds - de));      // add the crossing point
      }
    }
    poly = out;
  }
  return poly;
}

// ---- 4. Shading ------------------------------------------------------------------------------------

// Perspective-correct barycentric weights of the point (ndc_x, ndc_y) in the triangle with these
// clip-space corners. It solves for the weights directly from x, y and w, without dividing each corner
// by its w, so it also works when a corner is behind the camera.
RASTY_STAGE_HD Vec3f clip_barycentric(Vec4f a, Vec4f b, Vec4f c, float ndc_x, float ndc_y) {
  const Vec3f A = vec3(a.x, a.y, a.w);
  const Vec3f B = vec3(b.x, b.y, b.w);
  const Vec3f C = vec3(c.x, c.y, c.w);
  const Vec3f p = vec3(ndc_x, ndc_y, 1.0f);
  const float l0 = dot(cross(B, C), p);
  const float l1 = dot(cross(C, A), p);
  const float l2 = dot(cross(A, B), p);
  const float scale = 1.0f / (l0 + l1 + l2);  // make the weights add up to 1
  return vec3(l0 * scale, l1 * scale, l2 * scale);
}

// Center of pixel (x, y) in normalized device coordinates.
RASTY_STAGE_HD float pixel_ndc_x(const FrameConstants& k, int32_t x) {
  return ((float)x + 0.5f) * (2.0f / (float)k.width) - 1.0f;
}
RASTY_STAGE_HD float pixel_ndc_y(const FrameConstants& k, int32_t y) {
  return 1.0f - ((float)y + 0.5f) * (2.0f / (float)k.height);
}

// Color of pixel (x, y) for the triangle with these corners (clip positions, world normals, colors).
// The caller looks up the corners, since each backend stores them its own way.
RASTY_STAGE_HD uint32_t shade_sample(const FrameConstants& k, int32_t x, int32_t y, const Vec4f clip[3],
                                     const Vec3f normal[3], const Vec3f albedo[3]) {
  const Vec3f w = clip_barycentric(clip[0], clip[1], clip[2], pixel_ndc_x(k, x), pixel_ndc_y(k, y));
  const Vec3f n = interpolate_normal(w, normal[0], normal[1], normal[2], 1.0f, 1.0f, 1.0f);
  const Vec3f color = albedo[0] * w.x + albedo[1] * w.y + albedo[2] * w.z;
  return to_rgba8(shade_lambert(n, k.light_direction, color, k.ambient, k.diffuse));
}

}  // namespace rasty::pipeline

#undef RASTY_STAGE_HD
