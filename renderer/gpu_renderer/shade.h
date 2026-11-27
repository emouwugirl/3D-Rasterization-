#pragma once

#include <cstdint>

#include "pipeline.h"

namespace rasty {
    struct Triangle;
    struct Vec3f;

    namespace gpu {
        void shade(const pipeline::FrameConstants& constants,
           const unsigned long long* visibility,
           const Triangle* triangles,
           const pipeline::ScreenVertex* vertices,
           const math::Vec3f* world_normals,
           const rasty::Vec3f* colors,
           std::uint32_t* pixels);
    }
}