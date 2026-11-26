#pragma once

#include <cstddef>

#include "pipeline.h"

namespace rasty {
    struct Triangle;

    namespace gpu {
        void raster(const pipeline::FrameConstants& constants,
            const pipeline::ScreenVertex* vertices,
            const Triangle* triangles,
            std::size_t triangle_count,
            unsigned long long* visibility);
    }
}