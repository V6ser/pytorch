// !!! This is a file automatically generated by hipify!!!
#define TORCH_ASSERT_ONLY_METHOD_OPERATORS
#include <ATen/core/Tensor.h>
#include <ATen/Dispatch_v2.h>

#ifndef AT_PER_OPERATOR_HEADERS
#include <ATen/NativeFunctions.h>
#else
#include <ATen/ops/_local_scalar_dense_native.h>
#endif

#include <ATen\hip\HIPContext.h>

namespace at::native {

Scalar _local_scalar_dense_hip(const Tensor& self) {
  Scalar r;
  AT_DISPATCH_V2(
    self.scalar_type(), "_local_scalar_dense_hip", AT_WRAP([&] {
        scalar_t value;
        hipStream_t stream = at::hip::getCurrentHIPStream();
        at::cuda::memcpy_and_sync(&value, self.const_data_ptr<scalar_t>(), sizeof(scalar_t), hipMemcpyDeviceToHost, stream);
        r = Scalar(value);
      }), AT_EXPAND(AT_ALL_TYPES_AND_COMPLEX), kComplexHalf, kHalf, kBool, kBFloat16, AT_EXPAND(AT_BAREBONES_UNSIGNED_TYPES));
  return r;
}

} // at::native
