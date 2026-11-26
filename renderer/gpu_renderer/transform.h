#pragma once

#include <cstddef>

#include "pipeline.h"

namespace rasty {
struct Vec3f;  // Mesh vector 
}

namespace rasty::gpu {

void transform(const pipeline::FrameConstants& constants,
               const rasty::Vec3f* positions, 
               const rasty::Vec3f* normals, 
               std::size_t count, // # of vertices to process
               pipeline::ScreenVertex* vertices,
               math::Vec3f* world_normals);

}  
