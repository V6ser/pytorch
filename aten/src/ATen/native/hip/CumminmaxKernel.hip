// !!! This is a file automatically generated by hipify!!!
#define TORCH_ASSERT_NO_OPERATORS
#include <ATen/core/TensorBase.h>
#include <ATen/Dispatch.h>

#include <ATen\native\hip\ScanKernels.h>
#include <ATen\native\hip\ScanUtils.cuh>

#include <limits>
#include <functional>

namespace at::native {

void launch_cummax_hip_kernel(const TensorBase& self, const TensorBase& values, const TensorBase& indices, int64_t dim) {
  AT_DISPATCH_ALL_TYPES_AND3(at::ScalarType::Bool, at::ScalarType::Half, at::ScalarType::BFloat16,
    self.scalar_type(), "cummax_hip", [&]() {
    scalar_t init = self.is_floating_point() ? (-1*std::numeric_limits<scalar_t>::infinity()) : std::numeric_limits<scalar_t>::lowest();
    scan_dim_with_indices<scalar_t>(self, values, indices, dim, init, std::greater_equal<scalar_t>());
  });
}

void launch_cummin_hip_kernel(const TensorBase& self, const TensorBase& values, const TensorBase& indices, int64_t dim) {
  AT_DISPATCH_ALL_TYPES_AND3(at::ScalarType::Bool, at::ScalarType::Half, at::ScalarType::BFloat16,
    self.scalar_type(), "cummin_hip", [&]() {
    scalar_t init = self.is_floating_point() ? std::numeric_limits<scalar_t>::infinity() : std::numeric_limits<scalar_t>::max();
    scan_dim_with_indices<scalar_t>(self, values, indices, dim, init, std::less_equal<scalar_t>());
  });
}

} // namespace at::native
