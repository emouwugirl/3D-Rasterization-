#pragma once

#include <cuda_runtime.h>
#include <stdexcept>
#include <string>

//error checking, might not need in the future if we are just clauding
namespace rasty::gpu {

inline void check_cuda(cudaError_t result, const char* operation) {
  if (result != cudaSuccess)
    throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(result));
}

} 
