// Shared CPU/CUDA rasterization math.
// Conventions: right-handed world/view space, +Y up, camera looks down -Z,
// counter-clockwise model-space front faces, column-major matrices multiplied
// by column vectors, clip/NDC depth [0, 1] (near=0, far=1), screen row 0 at top,
// and pixel samples at (x + 0.5, y + 0.5). Screen coordinates use 8 subpixel bits.
#pragma once

#include <math.h>
#include <stdint.h>

#if defined(__CUDACC__)
#define RASTY_MATH_HD __host__ __device__ __forceinline__
#else
#define RASTY_MATH_HD inline
#endif

namespace rasty::math {

constexpr int kSubpixelBits = 8;
constexpr int32_t kSubpixelScale = 1 << kSubpixelBits;
constexpr int32_t kHalfSubpixel = kSubpixelScale / 2;
constexpr uint64_t kNoDepthId = UINT64_MAX;
constexpr uint32_t kNoTriangle = UINT32_MAX;

// The 16-byte alignment keeps vector loads predictable on CUDA. These are
// math vectors, separate from rasty::Vec3f, whose layout belongs to .bin files.
struct alignas(16) Vec2f { float x, y; };
struct alignas(16) Vec3f { float x, y, z, _pad; };
struct alignas(16) Vec4f { float x, y, z, w; };
struct IVec2 { int32_t x, y; };
struct UVec3 { uint32_t x, y, z; };
struct Mat4f { float values[16]; };  // values[column * 4 + row]
struct IRect { int32_t x0, y0, x1, y1; };

RASTY_MATH_HD float absf(float x) { return x < 0.0f ? -x : x; }
RASTY_MATH_HD float minf(float a, float b) { return a < b ? a : b; }
RASTY_MATH_HD float maxf(float a, float b) { return a > b ? a : b; }
RASTY_MATH_HD float clampf(float x, float lo, float hi) { return minf(maxf(x,lo),hi); }

RASTY_MATH_HD Vec2f vec2(float x, float y) { return {x, y}; }
RASTY_MATH_HD Vec3f vec3(float x, float y, float z) { return {x, y, z, 0.0f}; }
RASTY_MATH_HD Vec4f vec4(float x, float y, float z, float w) { return {x, y, z, w}; }

RASTY_MATH_HD Vec2f operator+(Vec2f a, Vec2f b) { return {a.x+b.x, a.y+b.y}; }
RASTY_MATH_HD Vec2f operator-(Vec2f a, Vec2f b) { return {a.x-b.x, a.y-b.y}; }
RASTY_MATH_HD Vec2f operator*(Vec2f v, float s) { return {v.x*s, v.y*s}; }
RASTY_MATH_HD Vec2f operator/(Vec2f v, float s) { return {v.x/s, v.y/s}; }
RASTY_MATH_HD Vec2f min(Vec2f a, Vec2f b) { return {a.x<b.x?a.x:b.x,a.y<b.y?a.y:b.y}; }
RASTY_MATH_HD Vec2f max(Vec2f a, Vec2f b) { return {a.x>b.x?a.x:b.x,a.y>b.y?a.y:b.y}; }
RASTY_MATH_HD Vec3f operator+(Vec3f a, Vec3f b) { return vec3(a.x+b.x,a.y+b.y,a.z+b.z); }
RASTY_MATH_HD Vec3f operator-(Vec3f a, Vec3f b) { return vec3(a.x-b.x,a.y-b.y,a.z-b.z); }
RASTY_MATH_HD Vec3f operator-(Vec3f v) { return vec3(-v.x,-v.y,-v.z); }
RASTY_MATH_HD Vec3f operator*(Vec3f v, float s) { return vec3(v.x*s,v.y*s,v.z*s); }
RASTY_MATH_HD Vec3f operator*(float s, Vec3f v) { return v*s; }
RASTY_MATH_HD Vec3f operator/(Vec3f v, float s) { return vec3(v.x/s,v.y/s,v.z/s); }
RASTY_MATH_HD Vec4f operator+(Vec4f a, Vec4f b) { return {a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w}; }
RASTY_MATH_HD Vec4f operator-(Vec4f a, Vec4f b) { return {a.x-b.x,a.y-b.y,a.z-b.z,a.w-b.w}; }
RASTY_MATH_HD Vec4f operator*(Vec4f v, float s) { return {v.x*s,v.y*s,v.z*s,v.w*s}; }
RASTY_MATH_HD Vec4f min(Vec4f a, Vec4f b) { return {a.x<b.x?a.x:b.x,a.y<b.y?a.y:b.y,a.z<b.z?a.z:b.z,a.w<b.w?a.w:b.w}; }
RASTY_MATH_HD Vec4f max(Vec4f a, Vec4f b) { return {a.x>b.x?a.x:b.x,a.y>b.y?a.y:b.y,a.z>b.z?a.z:b.z,a.w>b.w?a.w:b.w}; }
RASTY_MATH_HD Vec4f clamp(Vec4f v, float lo, float hi) {
  return {clampf(v.x,lo,hi),clampf(v.y,lo,hi),clampf(v.z,lo,hi),clampf(v.w,lo,hi)};
}

RASTY_MATH_HD float dot(Vec2f a, Vec2f b) { return a.x*b.x+a.y*b.y; }
RASTY_MATH_HD float dot(Vec3f a, Vec3f b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
RASTY_MATH_HD float dot(Vec4f a, Vec4f b) { return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w; }
RASTY_MATH_HD Vec3f cross(Vec3f a, Vec3f b) {
  return vec3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);
}
RASTY_MATH_HD Vec2f clamp(Vec2f v, float lo, float hi) { return {clampf(v.x,lo,hi),clampf(v.y,lo,hi)}; }
RASTY_MATH_HD float length(Vec2f v) { return sqrtf(dot(v,v)); }
RASTY_MATH_HD float length(Vec3f v) { return sqrtf(dot(v,v)); }
RASTY_MATH_HD Vec3f normalize(Vec3f v) { return v / sqrtf(dot(v,v)); }
RASTY_MATH_HD Vec3f normalize_or(Vec3f v, Vec3f fallback) {
  const float q = dot(v,v);
  return q > 1.0e-30f ? v / sqrtf(q) : fallback;
}
RASTY_MATH_HD Vec3f min(Vec3f a, Vec3f b) { return vec3(minf(a.x,b.x),minf(a.y,b.y),minf(a.z,b.z)); }
RASTY_MATH_HD Vec3f max(Vec3f a, Vec3f b) { return vec3(maxf(a.x,b.x),maxf(a.y,b.y),maxf(a.z,b.z)); }
RASTY_MATH_HD Vec3f clamp(Vec3f v, float lo, float hi) {
  return vec3(clampf(v.x,lo,hi),clampf(v.y,lo,hi),clampf(v.z,lo,hi));
}
RASTY_MATH_HD Vec3f lerp(Vec3f a, Vec3f b, float t) { return a*(1.0f-t)+b*t; }
RASTY_MATH_HD Vec2f lerp(Vec2f a, Vec2f b, float t) { return a*(1.0f-t)+b*t; }
RASTY_MATH_HD Vec4f lerp(Vec4f a, Vec4f b, float t) { return a*(1.0f-t)+b*t; }

RASTY_MATH_HD float& at(Mat4f& m, int row, int column) { return m.values[column*4+row]; }
RASTY_MATH_HD float at(const Mat4f& m, int row, int column) { return m.values[column*4+row]; }
RASTY_MATH_HD Mat4f identity() {
  Mat4f r{}; at(r,0,0)=at(r,1,1)=at(r,2,2)=at(r,3,3)=1.0f; return r;
}
RASTY_MATH_HD Mat4f transpose(Mat4f m) {
  Mat4f r{};
  for (int row=0; row<4; ++row) for (int col=0; col<4; ++col) at(r,row,col)=at(m,col,row);
  return r;
}
RASTY_MATH_HD Mat4f operator*(const Mat4f& a, const Mat4f& b) {
  Mat4f r{};
  for (int row=0; row<4; ++row) for (int col=0; col<4; ++col)
    for (int k=0; k<4; ++k) at(r,row,col) += at(a,row,k)*at(b,k,col);
  return r;
}
RASTY_MATH_HD Vec4f operator*(const Mat4f& m, Vec4f v) {
  return {at(m,0,0)*v.x+at(m,0,1)*v.y+at(m,0,2)*v.z+at(m,0,3)*v.w,
          at(m,1,0)*v.x+at(m,1,1)*v.y+at(m,1,2)*v.z+at(m,1,3)*v.w,
          at(m,2,0)*v.x+at(m,2,1)*v.y+at(m,2,2)*v.z+at(m,2,3)*v.w,
          at(m,3,0)*v.x+at(m,3,1)*v.y+at(m,3,2)*v.z+at(m,3,3)*v.w};
}
RASTY_MATH_HD Mat4f translation(Vec3f p) {
  Mat4f r=identity(); at(r,0,3)=p.x; at(r,1,3)=p.y; at(r,2,3)=p.z; return r;
}
RASTY_MATH_HD Mat4f scaling(Vec3f s) {
  Mat4f r{}; at(r,0,0)=s.x; at(r,1,1)=s.y; at(r,2,2)=s.z; at(r,3,3)=1.0f; return r;
}
RASTY_MATH_HD Mat4f rotation_x(float a) {
  Mat4f r=identity(); const float c=cosf(a), s=sinf(a);
  at(r,1,1)=c; at(r,1,2)=-s; at(r,2,1)=s; at(r,2,2)=c; return r;
}
RASTY_MATH_HD Mat4f rotation_y(float a) {
  Mat4f r=identity(); const float c=cosf(a), s=sinf(a);
  at(r,0,0)=c; at(r,0,2)=s; at(r,2,0)=-s; at(r,2,2)=c; return r;
}
RASTY_MATH_HD Mat4f rotation_z(float a) {
  Mat4f r=identity(); const float c=cosf(a), s=sinf(a);
  at(r,0,0)=c; at(r,0,1)=-s; at(r,1,0)=s; at(r,1,1)=c; return r;
}
// Euler angles are radians; applied X, then Y, then Z to a column vector.
RASTY_MATH_HD Mat4f model_matrix(Vec3f position, Vec3f rotation_xyz, Vec3f scale) {
  return translation(position)*rotation_z(rotation_xyz.z)*rotation_y(rotation_xyz.y)*rotation_x(rotation_xyz.x)*scaling(scale);
}
// Inverse-transpose of the model matrix's upper-left 3x3. Returns false for a
// singular transform; on success, result is a 4x4 matrix with no translation.
RASTY_MATH_HD bool normal_matrix(const Mat4f& model, Mat4f& result) {
  const float a=at(model,0,0), b=at(model,0,1), c=at(model,0,2);
  const float d=at(model,1,0), e=at(model,1,1), f=at(model,1,2);
  const float g=at(model,2,0), h=at(model,2,1), i=at(model,2,2);
  const float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
  if (absf(det)<1.0e-20f) return false;
  const float q=1.0f/det; result=identity();
  at(result,0,0)=(e*i-f*h)*q; at(result,0,1)=(f*g-d*i)*q; at(result,0,2)=(d*h-e*g)*q;
  at(result,1,0)=(c*h-b*i)*q; at(result,1,1)=(a*i-c*g)*q; at(result,1,2)=(b*g-a*h)*q;
  at(result,2,0)=(b*f-c*e)*q; at(result,2,1)=(c*d-a*f)*q; at(result,2,2)=(a*e-b*d)*q;
  return true;
}
RASTY_MATH_HD Vec3f transform_normal(const Mat4f& n, Vec3f v) {
  return normalize_or(vec3(at(n,0,0)*v.x+at(n,0,1)*v.y+at(n,0,2)*v.z,
                            at(n,1,0)*v.x+at(n,1,1)*v.y+at(n,1,2)*v.z,
                            at(n,2,0)*v.x+at(n,2,1)*v.y+at(n,2,2)*v.z),vec3(0,0,1));
}
RASTY_MATH_HD Mat4f look_at(Vec3f eye, Vec3f target, Vec3f up) {
  const Vec3f z=normalize(eye-target), x=normalize(cross(up,z)), y=cross(z,x);
  Mat4f r=identity();
  at(r,0,0)=x.x; at(r,0,1)=x.y; at(r,0,2)=x.z; at(r,0,3)=-dot(x,eye);
  at(r,1,0)=y.x; at(r,1,1)=y.y; at(r,1,2)=y.z; at(r,1,3)=-dot(y,eye);
  at(r,2,0)=z.x; at(r,2,1)=z.y; at(r,2,2)=z.z; at(r,2,3)=-dot(z,eye);
  return r;
}
// Right-handed perspective; clip z maps near to 0 and far to 1.
RASTY_MATH_HD Mat4f perspective(float fov_y, float aspect, float near_z, float far_z) {
  const float f=1.0f/tanf(fov_y*0.5f); Mat4f r{};
  at(r,0,0)=f/aspect; at(r,1,1)=f;
  at(r,2,2)=far_z/(near_z-far_z); at(r,2,3)=(far_z*near_z)/(near_z-far_z);
  at(r,3,2)=-1.0f; return r;
}
RASTY_MATH_HD Vec3f perspective_divide(Vec4f clip, float* inv_w=nullptr) {
  const float iw=1.0f/clip.w; if (inv_w) *inv_w=iw;
  return vec3(clip.x*iw,clip.y*iw,clip.z*iw);
}
RASTY_MATH_HD Vec2f viewport(Vec3f ndc, int32_t width, int32_t height) {
  return {(ndc.x+1.0f)*(float(width)*0.5f),(1.0f-ndc.y)*(float(height)*0.5f)};
}

struct ClipResult { Vec4f vertices[4]; int count; };
RASTY_MATH_HD float plane_distance(Vec4f p, int plane) {
  switch (plane) {
    case 0: return p.x+p.w; case 1: return p.w-p.x;
    case 2: return p.y+p.w; case 3: return p.w-p.y;
    case 4: return p.z;     default: return p.w-p.z;
  }
}
RASTY_MATH_HD bool all_outside_one_plane(Vec4f a, Vec4f b, Vec4f c) {
  for (int p=0; p<6; ++p) if (plane_distance(a,p)<0 && plane_distance(b,p)<0 && plane_distance(c,p)<0) return true;
  return false;
}
// Sutherland-Hodgman clip against z >= 0 (the near plane for this projection).
// The output is a polygon with 0, 3, or 4 vertices; triangulate a quad as a fan.
RASTY_MATH_HD ClipResult clip_near(Vec4f a, Vec4f b, Vec4f c) {
  const Vec4f in[3]={a,b,c}; ClipResult out{{},0};
  for (int i=0; i<3; ++i) {
    const Vec4f s=in[i], e=in[(i+1)%3]; const bool si=s.z>=0.0f, ei=e.z>=0.0f;
    if (si && ei) out.vertices[out.count++]=e;
    else if (si && !ei) {
      const float t=s.z/(s.z-e.z); out.vertices[out.count++]=s+(e-s)*t;
    } else if (!si && ei) {
      const float t=s.z/(s.z-e.z); out.vertices[out.count++]=s+(e-s)*t; out.vertices[out.count++]=e;
    }
  }
  return out;
}

RASTY_MATH_HD int32_t to_fixed(float pixel) { return (int32_t)roundf(pixel*(float)kSubpixelScale); }
RASTY_MATH_HD IVec2 to_fixed(Vec2f p) { return {to_fixed(p.x),to_fixed(p.y)}; }
RASTY_MATH_HD IVec2 pixel_center(int32_t x, int32_t y) {
  return {x*kSubpixelScale+kHalfSubpixel,y*kSubpixelScale+kHalfSubpixel};
}
// Signed edge value using the project's chosen orientation. For CCW world
// faces after the viewport Y flip, front-facing triangles have negative area.
RASTY_MATH_HD int64_t edge_function(IVec2 a, IVec2 b, IVec2 p) {
  return (int64_t(p.x)-a.x)*(int64_t(b.y)-a.y)-(int64_t(p.y)-a.y)*(int64_t(b.x)-a.x);
}
RASTY_MATH_HD int64_t edge_step_x(IVec2 a, IVec2 b) { return int64_t(b.y)-a.y; }
RASTY_MATH_HD int64_t edge_step_y(IVec2 a, IVec2 b) { return int64_t(a.x)-b.x; }
RASTY_MATH_HD bool front_facing(int64_t signed_area) { return signed_area < 0; }
RASTY_MATH_HD bool is_top_left(IVec2 a, IVec2 b) {
  const int32_t dx=b.x-a.x, dy=b.y-a.y; return dy<0 || (dy==0 && dx>0);
}
// Inputs are edge values for a front-facing (negative-area) triangle.
RASTY_MATH_HD bool inside_top_left(int64_t e0, bool tl0, int64_t e1, bool tl1, int64_t e2, bool tl2) {
  if (!tl0) --e0; if (!tl1) --e1; if (!tl2) --e2;
  return e0<=0 && e1<=0 && e2<=0;
}
RASTY_MATH_HD IRect bounding_box(IVec2 a, IVec2 b, IVec2 c, int32_t width, int32_t height) {
  int32_t minx=a.x, maxx=a.x, miny=a.y, maxy=a.y;
  minx=minx<b.x?minx:b.x; minx=minx<c.x?minx:c.x; maxx=maxx>b.x?maxx:b.x; maxx=maxx>c.x?maxx:c.x;
  miny=miny<b.y?miny:b.y; miny=miny<c.y?miny:c.y; maxy=maxy>b.y?maxy:b.y; maxy=maxy>c.y?maxy:c.y;
  const int32_t ax=minx-kHalfSubpixel, ay=miny-kHalfSubpixel;
  const int32_t bx=maxx-kHalfSubpixel, by=maxy-kHalfSubpixel;
  int32_t x0=ax/kSubpixelScale, y0=ay/kSubpixelScale;
  if (ax%kSubpixelScale>0) ++x0;  // ceil, accounting for C++ truncation toward zero
  if (ay%kSubpixelScale>0) ++y0;
  int32_t x1=bx/kSubpixelScale, y1=by/kSubpixelScale;
  if (bx%kSubpixelScale<0) --x1;  // floor for negative coordinates
  if (by%kSubpixelScale<0) --y1;
  ++x1; ++y1;
  x0=x0<0?0:x0; y0=y0<0?0:y0; x1=x1>width?width:x1; y1=y1>height?height:y1;
  return {x0,y0,x1,y1};
}
RASTY_MATH_HD Vec3f barycentric(int64_t e0, int64_t e1, int64_t e2, int64_t area) {
  const float inv=1.0f/(float)area; return {(float)e0*inv,(float)e1*inv,(float)e2*inv,0.0f};
}
RASTY_MATH_HD Vec3f barycentric_at_pixel(IVec2 a, IVec2 b, IVec2 c, IVec2 sample) {
  const int64_t area=edge_function(a,b,c);
  return barycentric(edge_function(b,c,sample),edge_function(c,a,sample),edge_function(a,b,sample),area);
}
RASTY_MATH_HD float interpolate_depth(Vec3f bary, float z0, float z1, float z2) {
  return bary.x*z0+bary.y*z1+bary.z*z2;
}
RASTY_MATH_HD Vec3f interpolate_perspective(Vec3f bary, Vec3f a, Vec3f b, Vec3f c,
                                             float inv_w0, float inv_w1, float inv_w2) {
  const float q=bary.x*inv_w0+bary.y*inv_w1+bary.z*inv_w2;
  return (a*(bary.x*inv_w0)+b*(bary.y*inv_w1)+c*(bary.z*inv_w2))/q;
}
RASTY_MATH_HD Vec2f interpolate_perspective(Vec3f bary, Vec2f a, Vec2f b, Vec2f c,
                                             float inv_w0, float inv_w1, float inv_w2) {
  const float q=bary.x*inv_w0+bary.y*inv_w1+bary.z*inv_w2;
  return (a*(bary.x*inv_w0)+b*(bary.y*inv_w1)+c*(bary.z*inv_w2))/q;
}
RASTY_MATH_HD Vec4f interpolate_perspective(Vec3f bary, Vec4f a, Vec4f b, Vec4f c,
                                             float inv_w0, float inv_w1, float inv_w2) {
  const float q=bary.x*inv_w0+bary.y*inv_w1+bary.z*inv_w2;
  return (a*(bary.x*inv_w0)+b*(bary.y*inv_w1)+c*(bary.z*inv_w2))*(1.0f/q);
}
RASTY_MATH_HD uint32_t quantize_depth(float depth) {
  const float d=clampf(depth,0.0f,1.0f);
  return d>=1.0f ? UINT32_MAX : (uint32_t)(d*4294967295.0f+0.5f);
}
RASTY_MATH_HD uint64_t pack_depth_and_id(float depth, uint32_t triangle_id) {
  return (uint64_t(quantize_depth(depth))<<32)|uint64_t(triangle_id);
}
RASTY_MATH_HD uint32_t id_from_key(uint64_t key) { return (uint32_t)key; }
RASTY_MATH_HD uint32_t depth_from_key(uint64_t key) { return (uint32_t)(key>>32); }

RASTY_MATH_HD Vec3f interpolate_normal(Vec3f bary, Vec3f n0, Vec3f n1, Vec3f n2,
                                        float iw0, float iw1, float iw2) {
  return normalize_or(interpolate_perspective(bary,n0,n1,n2,iw0,iw1,iw2),vec3(0,0,1));
}
RASTY_MATH_HD Vec3f shade_lambert(Vec3f normal, Vec3f light_direction, Vec3f albedo,
                                   float ambient, float diffuse) {
  const float ndotl=maxf(0.0f,dot(normalize_or(normal,vec3(0,0,1)),normalize_or(light_direction,vec3(0,0,1))));
  return albedo*(ambient+diffuse*ndotl);
}
RASTY_MATH_HD uint8_t to_unorm8(float c) { return (uint8_t)(clampf(c,0.0f,1.0f)*255.0f+0.5f); }
// Packed as 0xAABBGGRR (R occupies the least significant byte).
RASTY_MATH_HD uint32_t to_rgba8(Vec3f c, uint8_t alpha=255) {
  return uint32_t(to_unorm8(c.x))|(uint32_t(to_unorm8(c.y))<<8)|(uint32_t(to_unorm8(c.z))<<16)|(uint32_t(alpha)<<24);
}

}  // namespace rasty::math

#undef RASTY_MATH_HD
