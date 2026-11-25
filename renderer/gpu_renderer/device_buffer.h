#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>

#include "cuda_error.h"

namespace rasty::gpu {

// GPU memory
// allocates memory, copies results back, and automatically frees the allocation
template <typename T>
class DeviceBuffer {
  static_assert(std::is_trivially_copyable_v<T>);

 public:
  explicit DeviceBuffer(std::size_t count) : count_(count) {
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
      throw std::length_error("GPU buffer is too large");
    if (count != 0) {
      void* allocation = nullptr;
      check_cuda(cudaMalloc(&allocation, count * sizeof(T)), "Allocate GPU buffer");
      data_ = static_cast<T*>(allocation);
    }
  }

  ~DeviceBuffer() {
    if (data_) cudaFree(data_);
  }

  DeviceBuffer(const DeviceBuffer&) = delete;
  DeviceBuffer& operator=(const DeviceBuffer&) = delete;

  T* data() const { return data_; }

  // Source must contain at least count elements
  void upload(const T* source) {
    if (count_ != 0)
      check_cuda(cudaMemcpy(data_, source, count_ * sizeof(T), cudaMemcpyHostToDevice), "Upload GPU buffer");
  }

  // Destination must have room for count elements
  void download(T* destination) const {
    if (count_ != 0)
      check_cuda(cudaMemcpy(destination, data_, count_ * sizeof(T), cudaMemcpyDeviceToHost), "Download GPU buffer");
  }

 private:
  T* data_ = nullptr;
  std::size_t count_ = 0;
};

}
