#include "transform.h"

#include <algorithm>

#include "cuda_error.h"
#include "rasty/bin_format.hpp"

namespace rasty::gpu {
    namespace {
        __global__ void transform_kernel(pipeline::FrameConstants constants,
                                        const rasty::Vec3f* positions,
                                        const rasty::Vec3f* normals,
                                        std::size_t count,
                                        pipeline::ScreenVertex* vertices,
                                        math::Vec3f* world_normals) {
            // starting at blockidx*blockdim+threadidx
            const std::size_t start = std::size_t(blockIdx.x) * blockDim.x + threadIdx.x;
            // increment by griddim * blockdim (so each thread covers one index in each full run)
            const std::size_t stride = std::size_t(gridDim.x) * blockDim.x;

            for (std::size_t i = start; i < count; i += stride) {
                const auto& p = positions[i];
                const auto& n = normals[i];
                vertices[i] = pipeline::vertex_stage(constants, math::vec4(p.x, p.y, p.z, 1.0f));
                world_normals[i] = pipeline::world_normal(constants, math::vec3(n.x, n.y, n.z));
            }
        }
    }

    void transform(const pipeline::FrameConstants& constants,
               const rasty::Vec3f* positions, 
               const rasty::Vec3f* normals, 
               std::size_t count, // # of vertices to process
               pipeline::ScreenVertex* vertices,
               math::Vec3f* world_normals) {
        if (count == 0) {
            return;
        }

        constexpr unsigned THREADS = 256;
        // (count - 1) / threads + 1 calculates indices/thread and rounds up
        // block count capped at 65535
        const auto blocks = static_cast<unsigned>(std::min((count - 1) / THREADS + 1, std::size_t(65535)));
        transform_kernel<<<blocks, THREADS>>>(constants, positions, normals, count, vertices, world_normals);

        // check for errors
        check_cuda(cudaGetLastError(), "Launch transform kernel");
    }
}