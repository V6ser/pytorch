// !!! This is a file automatically generated by hipify!!!
#define TORCH_ASSERT_NO_OPERATORS

#include <ATen/native/UnaryOps.h>

#include <limits>

#include <ATen/AccumulateType.h>
#include <ATen/Dispatch.h>
#include <ATen/native/DispatchStub.h>
#include <ATen/native/Math.h>
#include <ATen/native/TensorIterator.h>
#include <ATen\native\hip\JitLoops.cuh>
#include <ATen\native\hip\Loops.cuh>
#include <ATen\native\hip\Math.cuh>
#include <ATen\native\hip\jit_utils.h>
#include <ATen/NumericUtils.h>
#include <c10/core/Scalar.h>
#include <c10/hip/HIPMathCompat.h>
#include <c10/util/complex.h>

namespace at::native {
        namespace {
            CONSTEXPR_EXCEPT_WIN_HIP char scaled_modified_bessel_k1_name[] = "scaled_modified_bessel_k1_forward";

            void scaled_modified_bessel_k1_kernel_hip(TensorIteratorBase& iterator) {
#if AT_USE_JITERATOR()
                AT_DISPATCH_FLOATING_TYPES(iterator.common_dtype(), "scaled_modified_bessel_k1_hip", [&]() {
                    jitted_gpu_kernel<scaled_modified_bessel_k1_name, scalar_t, scalar_t, 1>(iterator, scaled_modified_bessel_k1_string);
                });
#else
                AT_DISPATCH_FLOATING_TYPES(iterator.common_dtype(), "scaled_modified_bessel_k1_hip", [&]() {
                    gpu_kernel(iterator, []GPU_LAMBDA(scalar_t a) -> scalar_t {
                        return scaled_modified_bessel_k1_forward(a);
                    });
                });
#endif // AT_USE_JITERATOR()
            }
        }

        REGISTER_DISPATCH(special_scaled_modified_bessel_k1_stub, &scaled_modified_bessel_k1_kernel_hip);
} // namespace at::native
