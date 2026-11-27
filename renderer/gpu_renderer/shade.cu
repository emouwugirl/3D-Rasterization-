#include "shade.h"

#include <algorithm>
#include <cstddef>

#include "cuda_error.h"
#include "rasty/bin_format.hpp"

namespace rasty::gpu {
    namespace {
        __global__ void shade_kernel(pipeline::FrameConstants constants,
                                    const unsigned long long* visibility,
                                    const Triangle* triangles,
                                    const pipeline::ScreenVertex* vertices,
                                    const math::Vec3f* world_normals,
                                    const rasty::Vec3f* colors,
                                    std::uint32_t* pixels,
                                    std::size_t count) {
            const std::size_t start = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
            const std::size_t stride = std::size_t(gridDim.x) * blockDim.x;

            const auto clear_rgba = math::to_rgba8(constants.clear_color);

            // each thread is responsible for one index per stride
            for (std::size_t p = start; p < count; p += stride) {
                const auto key = visibility[p];

                // kNoDepthId means clear
                if (key == math::kNoDepthId) {
                    pixels[p] = clear_rgba;
                    continue;
                }

                const Triangle& tri = triangles[math::id_from_key(key)];
                math::Vec4f clip[3];
                math::Vec3f normals[3];
                math::Vec3f albedo[3];      // albedo is reflectivity

                for (int corner = 0; corner < 3; ++corner) {
                    const auto v = tri.v[corner];
                    clip[corner] = vertices[v].clip;
                    normals[corner] = world_normals[v];
                    albedo[corner] = colors ? math::vec3(colors[v].x, colors[v].y, colors[v].z) : constants.albedo;
                }

                // calculate x and y from the current iteration
                const auto x = static_cast<std::int32_t>(p % constants.width);
                const auto y = static_cast<std::int32_t>(p / constants.width);
                pixels[p] = pipeline::shade_sample(constants, x, y, clip, normals, albedo);
            }
        }
    }

    void shade(const pipeline::FrameConstants& constants,
            const unsigned long long* visibility,
            const Triangle* triangles,
            const pipeline::ScreenVertex* vertices,
            const math::Vec3f* world_normals,
            const rasty::Vec3f* colors,
            std::uint32_t* pixels) {
        const std::size_t count = std::size_t(constants.width) * constants.height;
        if (count == 0) {
            return;
        }

        constexpr unsigned THREADS = 256;
        const auto blocks = static_cast<unsigned>(std::min((count - 1) / THREADS + 1, std::size_t(65535)));

        shade_kernel<<<blocks, THREADS>>>(constants, visibility, triangles, vertices, world_normals, colors, pixels, count);
        check_cuda(cudaGetLastError(), "Launch shade kernel");
    }

}