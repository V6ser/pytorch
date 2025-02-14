// !!! This is a file automatically generated by hipify!!!
#ifndef CAFFE2_CORE_CONTEXT_GPU_H_
#define CAFFE2_CORE_CONTEXT_GPU_H_

#include <ctime>
#include <mutex>

#include "caffe2/core/common.h"
#include "caffe2/core/hip/common_gpu.h"
#include "caffe2/core/context.h"
#include "caffe2/core/context_base.h"
#include "caffe2/core/logging.h"
#include "caffe2/core/numa.h"
#include "caffe2/core/tensor.h"
#include "caffe2/core/types.h"
#include "caffe2/proto/caffe2_pb.h"

// Since we are using the macro CAFFE2_USE_MIOPEN, we will need to include this
// file after common.h is included.
#ifdef CAFFE2_USE_MIOPEN
#include "caffe2/core/hip/common_miopen.h"
#endif // CAFFE2_USE_MIOPEN

#include <c10/core/Device.h>
#include <c10/core/Stream.h>
#include <c10/hip/HIPStream.h>
#include <c10/hip/HIPGuard.h>

namespace caffe2 {

enum class HipMemoryPoolType {
  NONE = 0,
  CUB = 1,
  THC = 2,
};

/**
 * Gets the current memory pool type used by Caffe2.
 *
 * The memory pool is set up during caffe2's global initialization time.
 */
CAFFE2_HIP_API HipMemoryPoolType GetHipMemoryPoolType();

/**
 * A struct to host thread-local cuda objects.
 *
 * In Caffe2, each thread has its own non-default cuda stream as well as
 * related objects such as cublas and hiprand handles. This is achieved by
 * having the ThreadLocalHIPObjects wrapper that takes care of allocating
 * and deallocating these objects at the thread scope. This class is solely
 * used inside HIPContext and should not be used externally.
 *
 * This class manages the mapping from logical stream ID (int stream_id
 * passed around in Caffe2) and HIPStream objects.  We intend to eventually
 * deprecate the logical stream ID interface, but not for now.
 */
class CAFFE2_HIP_API ThreadLocalHIPObjects {
  friend class HIPContext;

 private:
  ThreadLocalHIPObjects() {
    for (DeviceIndex i = 0; i < C10_COMPILE_TIME_MAX_GPUS; ++i) {
      hip_streams_[i] = vector<c10::hip::HIPStream>();
    }
  }

  // Record current stream id for the current thread.
  // This is the new API we're trying to migrate use cases to and get rid of
  // explicit stream id passing. For now it's invoked in
  // HIPContext::SwitchToDevice
  void SetCurrentStreamId(DeviceIndex gpu, StreamId stream_id) {
    // TODO: use current device id from thread local instead of passing gpu in
    if (stream_id != -1) {
      c10::hip::setCurrentHIPStream(GetHIPStream(gpu, stream_id));
    }
  }

  // Retrieves the HIPStream corresponding to a logical stream ID, ensuring
  // that it exists in hip_streams_ if it has not been allocated yet.
  c10::hip::HIPStream GetHIPStream(DeviceIndex gpu, StreamId stream_id) {
    vector<c10::hip::HIPStream>& gpu_streams = hip_streams_[gpu];
    while (gpu_streams.size() <= static_cast<size_t>(stream_id)) {
      // NB: This streams are not guaranteed to be unique; we'll
      // wrap around once we run out of streams in the pool.
      gpu_streams.emplace_back(c10::hip::getStreamFromPool(/* high priority */ false, gpu));
    }
    return gpu_streams[stream_id];
  }

  // Uses the logical stream id from the thread local to pick the stream
  // We're going to migrate all usages to this case API instead of passing the
  // stream id directly
  hipStream_t GetStream(DeviceIndex gpu) {
    return c10::hip::getCurrentHIPStream(gpu).stream();
  }

  hipStream_t GetStream(DeviceIndex gpu, StreamId stream_id) {
    return GetHIPStream(gpu, stream_id).stream();
  }

  // Uses the logical stream id from the thread local to pick the stream
  // We're going to migrate all usages to this case API instead of passing the
  // stream id directly
  hipblasHandle_t GetHandle(DeviceIndex gpu) {
    return GetHandle(c10::hip::getCurrentHIPStream(gpu));
  }

  hipblasHandle_t GetHandle(c10::hip::HIPStream hip_stream) {
    HIPGuard guard(hip_stream.device_index());
    // Default construct in the map if it doesn't exist, and return a mutable
    // reference to it.
    auto& r = hipblas_handles_[hip_stream];
    if (r == nullptr) {
      HIPBLAS_ENFORCE(hipblasCreate(&r));
      // The default is HIPBLAS_POINTER_MODE_HOST. You can override
      // it after obtaining the cublas handle, but do that with
      // caution.
      HIPBLAS_ENFORCE(hipblasSetPointerMode(r, HIPBLAS_POINTER_MODE_HOST));
      HIPBLAS_ENFORCE(hipblasSetStream(r, hip_stream));
    }
    return r;
  }

#ifdef CAFFE2_USE_MIOPEN
  // Uses the logical stream id from the thread local to pick the stream
  // We're going to migrate all usages to this case API instead of passing the
  // stream id directly
  miopenHandle_t GetCudnnHandle(DeviceIndex gpu) {
    return GetCudnnHandle(c10::hip::getCurrentHIPStream(gpu));
  }

  miopenHandle_t GetCudnnHandle(c10::hip::HIPStream hip_stream) {
    HIPGuard guard(hip_stream.device_index());
    auto& r = miopen_handles_[hip_stream];
    if (r == nullptr) {
      MIOPEN_ENFORCE(miopenCreate(&r));
      MIOPEN_ENFORCE(miopenSetStream(r, hip_stream));
    }
    return r;
  }
#endif // CAFFE2_USE_MIOPEN

  ~ThreadLocalHIPObjects() noexcept {
    for (auto element : hipblas_handles_) {
      if (element.second) {
        HIPBLAS_CHECK(hipblasDestroy(element.second));
      }
    }
#ifdef CAFFE2_USE_MIOPEN
    for (auto element : miopen_handles_) {
      if (element.second) {
#ifdef _WIN32
        // this is because of something dumb in the ordering of
        // destruction. Sometimes at exit, the cuda context would already
        // be destroyed by the time this gets destroyed. This happens on
        // windows with cuda 11 and cuda 12.
        miopenDestroy(element.second);
#else
        MIOPEN_CHECK(miopenDestroy(element.second));
#endif // _WIN32
      }
    }
#endif // CAFFE2_USE_MIOPEN
  }
  // WARNING: mapping from logical stream ID to c10::hip::HIPStream
  // is NOT bijective; multiple logical stream IDs may map to the
  // same underlying stream ID.
  vector<c10::hip::HIPStream> hip_streams_[C10_COMPILE_TIME_MAX_GPUS];
  std::unordered_map<c10::hip::HIPStream, hipblasHandle_t> hipblas_handles_;
#ifdef CAFFE2_USE_MIOPEN
  std::unordered_map<c10::hip::HIPStream, miopenHandle_t> miopen_handles_;
#endif // CAFFE2_USE_MIOPEN
};

class CAFFE2_HIP_API HIPContext final : public BaseContext {
 public:
  // The default cuda context constructor.
  explicit HIPContext(DeviceIndex gpu_id = -1);
  explicit HIPContext(const DeviceOption& option);
  explicit HIPContext(Device device)
      : HIPContext(DeviceToOption(device)) {}

  ~HIPContext() override;

  inline void SwitchToDevice(StreamId stream_id) override {
    getHipObjects().SetCurrentStreamId(gpu_id_, stream_id);
    CaffeHipSetDevice(gpu_id_);
  }

  // void SwitchToDevice()
  using BaseContext::SwitchToDevice;

  inline void WaitEvent(const Event& ev) override {
    ev.Wait(HIP, this);
  }

  inline void Record(Event* ev, const char* err_msg = nullptr) const override {
    CAFFE_ENFORCE(ev, "Event must not be null.");
    ev->Record(HIP, this, err_msg);
  }

  // Note on current use cases:
  // FinishDeviceComputation must be called on the same cpu thread as
  // SwitchToDevice()
  void FinishDeviceComputation() override {
    HIP_ENFORCE(hipStreamSynchronize(getHipObjects().GetStream(gpu_id_)));
  }

  inline int device_id() const {
    return gpu_id_;
  }

  inline c10::hip::HIPStream stream() const {
    return at::cuda::getStreamFromExternal(getHipObjects().GetStream(gpu_id_), gpu_id_);
  }

  inline hipStream_t hip_stream() const {
    return getHipObjects().GetStream(gpu_id_);
  }

  static hipStream_t hip_stream(DeviceIndex gpu_id, StreamId stream_id) {
    return getHipObjects().GetStream(gpu_id, stream_id);
  }

  hipblasHandle_t hipblas_handle() {
    return getHipObjects().GetHandle(gpu_id_);
  }

#ifdef CAFFE2_USE_MIOPEN
  miopenHandle_t miopen_handle() {
    return getHipObjects().GetCudnnHandle(gpu_id_);
  }
#endif // CAFFE2_USE_MIOPEN

  hiprandGenerator_t& hiprand_generator() {
    if (!hiprand_generator_) {
      HIPGuard guard(gpu_id_);
      HIPRAND_ENFORCE(
          hiprandCreateGenerator(&hiprand_generator_, HIPRAND_RNG_PSEUDO_DEFAULT));
      HIPRAND_ENFORCE(
          hiprandSetPseudoRandomGeneratorSeed(hiprand_generator_, random_seed_));
      TORCH_CHECK_NOTNULL(hiprand_generator_);
    }
    HIPRAND_ENFORCE(hiprandSetStream(hiprand_generator_, hip_stream()));
    return hiprand_generator_;
  }

  inline static at::DataPtr New(size_t nbytes) {
    return GetAllocator(HIP)->allocate(nbytes);
  }

  // Get a mutex to lock out hipMalloc / hipFree calls when
  // NCCL kernels are being launched. Should remove threat of
  // deadlocks
  static std::mutex& mutex();

  // Functions to query memory stats. Only available if flag
  // --caffe2_gpu_memory_tracking is enabled.
  static std::vector<long> TotalMemoryByGpu();
  static std::vector<long> MaxMemoryByGpu();

  template <class SrcContext, class DstContext>
  inline void CopyBytes(size_t nbytes, const void* src, void* dst) {
    HIP_ENFORCE(hipMemcpyAsync(
        dst,
        src,
        nbytes,
        hipMemcpyDefault,
        getHipObjects().GetStream(gpu_id_)));
  }

  void CopyBytesSameDevice(size_t nbytes, const void* src, void* dst) override {
    CopyBytes<HIPContext, HIPContext>(nbytes, src, dst);
  }

  void CopyBytesToCPU(size_t nbytes, const void* src, void* dst) override {
    CopyBytes<HIPContext, CPUContext>(nbytes, src, dst);
  }

  void CopyBytesFromCPU(size_t nbytes, const void* src, void* dst) override {
    CopyBytes<CPUContext, HIPContext>(nbytes, src, dst);
  }

  template <typename T, class SrcContext, class DstContext>
  inline void Copy(int n, const T* src, T* dst) {
    CopyBytes<SrcContext, DstContext>(n * sizeof(T),
                                 static_cast<const void*>(src),
                                 static_cast<void*>(dst));
  }

  template <class SrcContext, class DstContext>
  inline void
  CopyItems(const TypeMeta meta, size_t n, const void* src, void* dst) {
    CAFFE_ENFORCE(!meta.copy(), "HIPContext requires fundamental types.");
    CopyBytes<SrcContext, DstContext>(n * meta.itemsize(), src, dst);
  }

  static void CopyBytesAsync(
      size_t nbytes,
      const void* src,
      Device src_device,
      void* dst,
      Device dst_device);
  static void CopyBytesSync(
      size_t nbytes,
      const void* src,
      Device src_device,
      void* dst,
      Device dst_device);

  // By default HIP operators have async device parts
  static bool HasAsyncPartDefault() {
    return true;
  }

  static bool SupportsAsyncScheduling() {
    return true;
  }

  static bool IsStreamFree(const DeviceOption& option, StreamId stream_id) {
    const auto stream = HIPContext::hip_stream(option.device_id(), stream_id);
    const auto status = C10_HIP_ERROR_HANDLED(hipStreamQuery(stream));
    if (status == hipErrorNotReady) {
      // ignore and clear the error if not ready
      C10_HIP_CLEAR_ERROR();
    } else {
      C10_HIP_CHECK(status); // Reraise error
    }
    return status == hipSuccess;
  }

  at::Device device() const override {
    return at::Device(HIP, gpu_id_);
  }

  DeviceType device_type() const override {
    return HIP;
  }

  static constexpr DeviceType GetDeviceType() {
    return HIP;
  }

 protected:
  int gpu_id_;
  int random_seed_;
  hiprandGenerator_t hiprand_generator_{nullptr};
  static ThreadLocalHIPObjects& getHipObjects();
};

using TensorHIP = Tensor;

}  // namespace caffe2

#endif  // CAFFE2_CORE_CONTEXT_GPU_H_
