/* Copyright 2019 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef XLA_STREAM_EXECUTOR_GPU_ASM_COMPILER_H_
#define XLA_STREAM_EXECUTOR_GPU_ASM_COMPILER_H_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "xla/stream_executor/gpu/gpu_asm_opts.h"
#include "xla/stream_executor/kernel.h"
#include "xla/stream_executor/stream_executor.h"
#include "tsl/platform/statusor.h"
#if GOOGLE_CUDA
#include "third_party/gpus/cuda/include/cuda.h"
#include "xla/stream_executor/cuda/cuda_driver.h"
#endif  // GOOGLE_CUDA

// TODO(hebecker): Remove this once triton_kernels from jaxlib has been updated.
#ifdef JAX_GPU_CUDA
#include "xla/stream_executor/cuda/cuda_asm_compiler.h"  // IWYU pragma: export
#endif

namespace stream_executor {

struct HsacoImage {
  std::string gfx_arch;
  std::vector<uint8_t> bytes;
};

// Bundles the GPU machine code (HSA Code Object) and returns the resulting
// binary (i.e. a fatbin) as a byte array.
absl::StatusOr<std::vector<uint8_t>> BundleGpuAsm(
    std::vector<HsacoImage> images, const std::string rocm_root_dir);

#if GOOGLE_CUDA
// Maintains a cache of pointers to loaded kernels
template <typename... Args>
absl::StatusOr<TypedKernel<Args...>*> LoadKernelOrGetPtr(
    StreamExecutor* executor, absl::string_view kernel_name,
    absl::string_view ptx, absl::Span<const uint8_t> cubin_data) {
  using KernelPtrCacheKey =
      std::tuple<CUcontext, absl::string_view, absl::string_view>;

  static absl::Mutex kernel_ptr_cache_mutex(absl::kConstInit);
  static auto& kernel_ptr_cache ABSL_GUARDED_BY(kernel_ptr_cache_mutex) =
      *new absl::node_hash_map<KernelPtrCacheKey, TypedKernel<Args...>>();
  CUcontext current_context = cuda::CurrentContextOrDie();
  KernelPtrCacheKey kernel_ptr_cache_key{current_context, kernel_name, ptx};
  absl::MutexLock lock(&kernel_ptr_cache_mutex);

  auto it = kernel_ptr_cache.find(kernel_ptr_cache_key);
  if (it == kernel_ptr_cache.end()) {
    TF_ASSIGN_OR_RETURN(
        TypedKernel<Args...> loaded,
        (TypedKernel<Args...>::Create(executor, kernel_name, ptx, cubin_data)));
    it =
        kernel_ptr_cache.emplace(kernel_ptr_cache_key, std::move(loaded)).first;
  }

  CHECK(it != kernel_ptr_cache.end());
  return &it->second;
}
#endif  // GOOGLE_CUDA

}  // namespace stream_executor

#endif  // XLA_STREAM_EXECUTOR_GPU_ASM_COMPILER_H_
