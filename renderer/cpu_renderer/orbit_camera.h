// RenderSettings for a camera that orbits a model, used by the GUI bindings and the benchmark so both
// draw the same view. The model is first moved and scaled into the unit sphere at the origin, so the
// same yaw, pitch and distance frame every model the same way.
#pragma once

#include <cmath>
#include <cstdint>

#include "rasty/bin_format.hpp"
#include "renderer.h"

namespace rasty {

struct OrbitView {
  float yaw = 0.6f;         // radians around +Y, 0 = looking down -Z
  float pitch = 0.25f;      // radians above the horizon, kept within +-1.5
  float distance = 3.0f;    // from the model's center, in model radii; 3 keeps the unit sphere in view
  float fov_y = 0.6981317f; // 40 degrees
};

inline RenderSettings orbit_settings(const Bounds& bounds, std::uint32_t width, std::uint32_t height,
                                     const OrbitView& view) {
  using math::Vec3f;  // the math vector, not the .bin file's rasty::Vec3f
  using namespace math;
  const float radius = bounds.radius > 0.0f ? bounds.radius : 1.0f;
  const float pitch = std::fmax(-1.5f, std::fmin(1.5f, view.pitch));
  const float distance = std::fmax(0.05f, view.distance);

  RenderSettings s;
  s.width = width;
  s.height = height;
  s.model = scaling(vec3(1.0f / radius, 1.0f / radius, 1.0f / radius)) *
            translation(vec3(-bounds.center.x, -bounds.center.y, -bounds.center.z));

  const Vec3f eye = vec3(distance * std::cos(pitch) * std::sin(view.yaw), distance * std::sin(pitch),
                         distance * std::cos(pitch) * std::cos(view.yaw));
  s.view = look_at(eye, vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));

  // Near and far hug the unit sphere, which keeps the 32-bit depth keys as fine as possible.
  const float near_z = std::fmax(0.01f, distance - 1.05f);
  const float far_z = distance + 1.05f;
  const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
  s.projection = perspective(view.fov_y, aspect, near_z, far_z);

  // A light above and to the left of the camera, so the model is lit from the viewer's side.
  const Vec3f forward = normalize(eye);  // from the model toward the camera
  const Vec3f right = normalize_or(cross(vec3(0.0f, 1.0f, 0.0f), forward), vec3(1.0f, 0.0f, 0.0f));
  const Vec3f up = cross(forward, right);
  s.light_direction = normalize(forward + up * 0.6f - right * 0.4f);
  s.clear_color = vec3(0.094f, 0.094f, 0.094f);  // the GUI canvas's #181818
  return s;
}

}  // namespace rasty
