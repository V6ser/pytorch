// !!! This is a file automatically generated by hipify!!!
#ifndef CAFFE2_CORE_COMMON_GPU_H_
#define CAFFE2_CORE_COMMON_GPU_H_

#include <assert.h>
#include <hip/hip_runtime.h>
#include <hip/hip_runtime.h>

#if !defined(USE_ROCM)
#ifdef __GNUC__
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC diagnostic push
#endif
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif // __GNUC__
#endif // USE_ROCM

#include <hipblas/hipblas.h>
#include <hiprand/hiprand.h>
#include <hip/driver_types.h>

#include "caffe2/core/common.h"
#include "caffe2/core/logging.h"

#include "c10/hip/HIPMacros.h"
#include "c10/hip/HIPMathCompat.h"
#include <c10/hip/HIPGuard.h>

#define CAFFE2_HIP_EXPORT C10_EXPORT

// CAFFE2_HIP_API gets translated to CAFFE2_HIP_API in hipify script, which
// causes a marco redefinition issue with the later definition of
// CAFFE2_HIP_API, so we exclude this definition when HIP is specified
#if !defined(USE_ROCM)
#define CAFFE2_HIP_API TORCH_HIP_CPP_API
#endif // USE_ROCM

//TODO: [ROCm] Need to remove this after HIP->HIP mapping is updated.
#define CAFFE2_HIP_EXPORT C10_EXPORT
#define CAFFE2_HIP_API TORCH_HIP_API

// This is a macro defined for cuda fp16 support. In default, cuda fp16 is
// supported by NVCC 7.5, but it is also included in the Tegra X1 platform with
// a (custom?) NVCC 7.0. As a result, we would normally just check the cuda
// version here, but would also allow a use to pass in the flag
// CAFFE_HAS_HIP_FP16 manually.

#ifndef CAFFE_HAS_HIP_FP16
#define CAFFE_HAS_HIP_FP16
#endif // CAFFE_HAS_HIP_FP16

#ifdef CAFFE_HAS_HIP_FP16
#include <hip/hip_fp16.h>
#endif

// cuda major revision number below which fp16 compute is not supoorted
#if !defined(USE_ROCM)
constexpr int kFp16HIPDevicePropMajor = 6;
#else
constexpr int kFp16HIPDevicePropMajor = 3;
#endif

// Re-enable strict aliasing diagnostic if it was disabled.
#if !defined(USE_ROCM)
#ifdef __GNUC__
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC diagnostic pop
#endif
#endif // __GNUC__
#endif // USE_ROCM

/**
 * The maximum number of peers that each gpu can have when doing p2p setup.
 * Currently, according to NVidia documentation, each device can support a
 * system-wide maximum of eight peer connections.
 * When Caffe2 sets up peer access resources, if we have more than 8 gpus,
 * we will enable peer access in groups of 8.
 */
#define CAFFE2_HIP_MAX_PEER_SIZE 8

namespace caffe2 {

#if !defined(USE_ROCM)
/**
 * Empty class to identify TensorCore-based math
 */
class TensorCoreEngine {};
#endif // USE_ROCM

#if !defined(USE_ROCM) || (defined(USE_ROCM) && ROCM_VERSION >= 50700)
#define CAFFE2_HIP_PTRATTR_MEMTYPE type
#else
#define CAFFE2_HIP_PTRATTR_MEMTYPE memoryType
#endif

/**
 * A runtime function to report the cuda version that Caffe2 is built with.
 */
inline int HipVersion() {
#if defined(USE_ROCM)
  return ROCM_VERSION;
#else
  return TORCH_HIP_VERSION;
#endif
}

/**
 * Returns the number of devices.
 */
CAFFE2_HIP_API int NumHipDevices();

/**
 * Check if the current running session has a cuda gpu present.
 *
 * Note that this is different from having caffe2 built with cuda. Building
 * Caffe2 with cuda only guarantees that this function exists. If there are no
 * cuda gpus present in the machine, or there are hardware configuration
 * problems like an insufficient driver, this function will still return false,
 * meaning that there is no usable GPU present.
 *
 * In the open source build, it is possible that Caffe2's GPU code is
 * dynamically loaded, and as a result a library could be only linked to the
 * CPU code, but want to test if cuda is later available or not. In this case,
 * one should use HasHipRuntime() from common.h.
 */
inline bool HasHipGPU() {
  return NumHipDevices() > 0;
}

/**
 * Gets the current GPU id. This is a simple wrapper around hipGetDevice().
 */
CAFFE2_HIP_API int CaffeHipGetDevice();

/**
 * Gets the current GPU id. This is a simple wrapper around hipGetDevice().
 */
CAFFE2_HIP_API void CaffeHipSetDevice(const int id);

/**
 * Gets the GPU id that the current pointer is located at.
 */
CAFFE2_HIP_API int GetGPUIDForPointer(const void* ptr);

/**
 * Gets the device property for the given device. This function is thread safe.
 * The initial run on this function is ~1ms/device; however, the results are
 * cached so subsequent runs should be much faster.
 */
CAFFE2_HIP_API const hipDeviceProp_t& GetDeviceProperty(const int device);

/**
 * Runs a device query function and prints out the results to LOG(INFO).
 */
CAFFE2_HIP_API void DeviceQuery(const int deviceid);

/**
 * Return a peer access pattern by returning a matrix (in the format of a
 * nested vector) of boolean values specifying whether peer access is possible.
 *
 * This function returns false if anything wrong happens during the query of
 * the GPU access pattern.
 */
CAFFE2_HIP_API bool GetHipPeerAccessPattern(vector<vector<bool>>* pattern);

/**
 * Return the availability of TensorCores for math
 */
CAFFE2_HIP_API bool TensorCoreAvailable();

/**
 * Return a human readable cublas error string.
 */
CAFFE2_HIP_API const char* hipblasGetErrorString(hipblasStatus_t error);

/**
 * Return a human readable hiprand error string.
 */
CAFFE2_HIP_API const char* hiprandGetErrorString(hiprandStatus_t error);

// HIP: various checks for different function calls.
#define HIP_ENFORCE(condition, ...) \
  do {                               \
    hipError_t error = condition;   \
    CAFFE_ENFORCE_EQ(                \
        error,                       \
        hipSuccess,                 \
        "Error at: ",                \
        __FILE__,                    \
        ":",                         \
        __LINE__,                    \
        ": ",                        \
        hipGetErrorString(error),   \
        ##__VA_ARGS__);              \
  } while (0)
#define HIP_CHECK(condition)                                 \
  do {                                                        \
    hipError_t error = condition;                            \
    CHECK(error == hipSuccess) << hipGetErrorString(error); \
  } while (0)

#define HIP_DRIVERAPI_ENFORCE(condition)                            \
  do {                                                               \
    hipError_t result = condition;                                     \
    if (result != hipSuccess) {                                    \
      const char* msg;                                               \
      hipGetErrorName(result, &msg);                                  \
      CAFFE_THROW("Error at: ", __FILE__, ":", __LINE__, ": ", msg); \
    }                                                                \
  } while (0)
#define HIP_DRIVERAPI_CHECK(condition)                                 \
  do {                                                                  \
    hipError_t result = condition;                                        \
    if (result != hipSuccess) {                                       \
      const char* msg;                                                  \
      hipGetErrorName(result, &msg);                                     \
      LOG(FATAL) << "Error at: " << __FILE__ << ":" << __LINE__ << ": " \
                 << msg;                                                \
    }                                                                   \
  } while (0)

#define HIPBLAS_ENFORCE(condition)                \
  do {                                           \
    hipblasStatus_t status = condition;           \
    CAFFE_ENFORCE_EQ(                            \
        status,                                  \
        HIPBLAS_STATUS_SUCCESS,                   \
        "Error at: ",                            \
        __FILE__,                                \
        ":",                                     \
        __LINE__,                                \
        ": ",                                    \
        ::caffe2::hipblasGetErrorString(status)); \
  } while (0)
#define HIPBLAS_CHECK(condition)                    \
  do {                                             \
    hipblasStatus_t status = condition;             \
    CHECK(status == HIPBLAS_STATUS_SUCCESS)         \
        << ::caffe2::hipblasGetErrorString(status); \
  } while (0)

#define HIPRAND_ENFORCE(condition)                \
  do {                                           \
    hiprandStatus_t status = condition;           \
    CAFFE_ENFORCE_EQ(                            \
        status,                                  \
        HIPRAND_STATUS_SUCCESS,                   \
        "Error at: ",                            \
        __FILE__,                                \
        ":",                                     \
        __LINE__,                                \
        ": ",                                    \
        ::caffe2::hiprandGetErrorString(status)); \
  } while (0)
#define HIPRAND_CHECK(condition)                    \
  do {                                             \
    hiprandStatus_t status = condition;             \
    CHECK(status == HIPRAND_STATUS_SUCCESS)         \
        << ::caffe2::hiprandGetErrorString(status); \
  } while (0)

#define HIP_1D_KERNEL_LOOP(i, n)                                 \
  for (size_t i = blockIdx.x * blockDim.x + threadIdx.x; i < (n); \
       i += blockDim.x * gridDim.x)

#define HIP_2D_KERNEL_LOOP(i, n, j, m)                             \
  for (size_t i = blockIdx.x * blockDim.x + threadIdx.x; i < (n);   \
       i += blockDim.x * gridDim.x)                                 \
    for (size_t j = blockIdx.y * blockDim.y + threadIdx.y; j < (m); \
         j += blockDim.y * gridDim.y)

// The following helper functions are here so that you can write a kernel call
// when you are not particularly interested in maxing out the kernels'
// performance. Usually, this will give you a reasonable speed, but if you
// really want to find the best performance, it is advised that you tune the
// size of the blocks and grids more reasonably.
// A legacy note: this is derived from the old good Caffe days, when I simply
// hard-coded the number of threads and wanted to keep backward compatibility
// for different computation capabilities.
// For more info on HIP compute capabilities, visit the NVidia website at:
//    http://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#compute-capabilities

// The number of cuda threads to use. Since work is assigned to SMs at the
// granularity of a block, 128 is chosen to allow utilizing more SMs for
// smaller input sizes.
// 1D grid
constexpr int CAFFE_HIP_NUM_THREADS = 128;
// 2D grid
constexpr int CAFFE_HIP_NUM_THREADS_2D_DIMX = 16;
constexpr int CAFFE_HIP_NUM_THREADS_2D_DIMY = 16;

// The maximum number of blocks to use in the default kernel call. We set it to
// 4096 which would work for compute capability 2.x (where 65536 is the limit).
// This number is very carelessly chosen. Ideally, one would like to look at
// the hardware at runtime, and pick the number of blocks that makes most
// sense for the specific runtime environment. This is a todo item.
// 1D grid
constexpr int CAFFE_MAXIMUM_NUM_BLOCKS = 4096;
// 2D grid
constexpr int CAFFE_MAXIMUM_NUM_BLOCKS_2D_DIMX = 128;
constexpr int CAFFE_MAXIMUM_NUM_BLOCKS_2D_DIMY = 128;

constexpr int kHIPGridDimMaxX = 2147483647;
constexpr int kHIPGridDimMaxY = 65535;
constexpr int kHIPGridDimMaxZ = 65535;

/**
 * @brief Compute the number of blocks needed to run N threads.
 */
inline int CAFFE_GET_BLOCKS(const int N) {
  return std::max(
      std::min(
          (N + CAFFE_HIP_NUM_THREADS - 1) / CAFFE_HIP_NUM_THREADS,
          CAFFE_MAXIMUM_NUM_BLOCKS),
      // Use at least 1 block, since HIP does not allow empty block
      1);
}

/**
 * @brief Compute the number of blocks needed to run N threads for a 2D grid
 */
inline dim3 CAFFE_GET_BLOCKS_2D(const int N, const int /* M */) {
  dim3 grid;
  // Not calling the 1D version for each dim to keep all constants as literals

  grid.x = std::max(
      std::min(
          (N + CAFFE_HIP_NUM_THREADS_2D_DIMX - 1) /
              CAFFE_HIP_NUM_THREADS_2D_DIMX,
          CAFFE_MAXIMUM_NUM_BLOCKS_2D_DIMX),
      // Use at least 1 block, since HIP does not allow empty block
      1);

  grid.y = std::max(
      std::min(
          (N + CAFFE_HIP_NUM_THREADS_2D_DIMY - 1) /
              CAFFE_HIP_NUM_THREADS_2D_DIMY,
          CAFFE_MAXIMUM_NUM_BLOCKS_2D_DIMY),
      // Use at least 1 block, since HIP does not allow empty block
      1);

  return grid;
}

using HIPGuard = c10::hip::HIPGuard;

template <typename T, int N>
struct SimpleArray {
  T data[N];
};

constexpr int kHIPTensorMaxDims = 8;

#define DISPATCH_FUNCTION_BY_VALUE_WITH_TYPE_1(val, Func, T, ...) \
  do {                                                            \
    CAFFE_ENFORCE_LE(val, kHIPTensorMaxDims);                    \
    switch (val) {                                                \
      case 1: {                                                   \
        Func<T, 1>(__VA_ARGS__);                                  \
        break;                                                    \
      }                                                           \
      case 2: {                                                   \
        Func<T, 2>(__VA_ARGS__);                                  \
        break;                                                    \
      }                                                           \
      case 3: {                                                   \
        Func<T, 3>(__VA_ARGS__);                                  \
        break;                                                    \
      }                                                           \
      case 4: {                                                   \
        Func<T, 4>(__VA_ARGS__);                                  \
        break;                                                    \
      }                                                           \
      case 5: {                                                   \
        Func<T, 5>(__VA_ARGS__);                                  \
        break;                                                    \
      }                                                           \
      case 6: {                                                   \
        Func<T, 6>(__VA_ARGS__);                                  \
        break;                                                    \
      }                                                           \
      case 7: {                                                   \
        Func<T, 7>(__VA_ARGS__);                                  \
        break;                                                    \
      }                                                           \
      case 8: {                                                   \
        Func<T, 8>(__VA_ARGS__);                                  \
        break;                                                    \
      }                                                           \
      default: {                                                  \
        break;                                                    \
      }                                                           \
    }                                                             \
  } while (false)

#define DISPATCH_FUNCTION_BY_VALUE_WITH_TYPE_2(val, Func, T1, T2, ...) \
  do {                                                                 \
    CAFFE_ENFORCE_LE(val, kHIPTensorMaxDims);                         \
    switch (val) {                                                     \
      case 1: {                                                        \
        Func<T1, T2, 1>(__VA_ARGS__);                                  \
        break;                                                         \
      }                                                                \
      case 2: {                                                        \
        Func<T1, T2, 2>(__VA_ARGS__);                                  \
        break;                                                         \
      }                                                                \
      case 3: {                                                        \
        Func<T1, T2, 3>(__VA_ARGS__);                                  \
        break;                                                         \
      }                                                                \
      case 4: {                                                        \
        Func<T1, T2, 4>(__VA_ARGS__);                                  \
        break;                                                         \
      }                                                                \
      case 5: {                                                        \
        Func<T1, T2, 5>(__VA_ARGS__);                                  \
        break;                                                         \
      }                                                                \
      case 6: {                                                        \
        Func<T1, T2, 6>(__VA_ARGS__);                                  \
        break;                                                         \
      }                                                                \
      case 7: {                                                        \
        Func<T1, T2, 7>(__VA_ARGS__);                                  \
        break;                                                         \
      }                                                                \
      case 8: {                                                        \
        Func<T1, T2, 8>(__VA_ARGS__);                                  \
        break;                                                         \
      }                                                                \
      default: {                                                       \
        break;                                                         \
      }                                                                \
    }                                                                  \
  } while (false)

#define DISPATCH_FUNCTION_BY_VALUE_WITH_TYPE_3(val, Func, T1, T2, T3, ...) \
  do {                                                                     \
    CAFFE_ENFORCE_LE(val, kHIPTensorMaxDims);                             \
    switch (val) {                                                         \
      case 1: {                                                            \
        Func<T1, T2, T3, 1>(__VA_ARGS__);                                  \
        break;                                                             \
      }                                                                    \
      case 2: {                                                            \
        Func<T1, T2, T3, 2>(__VA_ARGS__);                                  \
        break;                                                             \
      }                                                                    \
      case 3: {                                                            \
        Func<T1, T2, T3, 3>(__VA_ARGS__);                                  \
        break;                                                             \
      }                                                                    \
      case 4: {                                                            \
        Func<T1, T2, T3, 4>(__VA_ARGS__);                                  \
        break;                                                             \
      }                                                                    \
      case 5: {                                                            \
        Func<T1, T2, T3, 5>(__VA_ARGS__);                                  \
        break;                                                             \
      }                                                                    \
      case 6: {                                                            \
        Func<T1, T2, T3, 6>(__VA_ARGS__);                                  \
        break;                                                             \
      }                                                                    \
      case 7: {                                                            \
        Func<T1, T2, T3, 7>(__VA_ARGS__);                                  \
        break;                                                             \
      }                                                                    \
      case 8: {                                                            \
        Func<T1, T2, T3, 8>(__VA_ARGS__);                                  \
        break;                                                             \
      }                                                                    \
      default: {                                                           \
        break;                                                             \
      }                                                                    \
    }                                                                      \
  } while (false)

} // namespace caffe2

#endif // CAFFE2_CORE_COMMON_GPU_H_
