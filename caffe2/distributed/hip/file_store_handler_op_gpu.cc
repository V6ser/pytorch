// !!! This is a file automatically generated by hipify!!!
#include "caffe2/distributed/file_store_handler_op.h"

#if !defined(USE_ROCM)
#include <caffe2/core/hip/context_gpu.h>
#else
#include <caffe2/core/hip/context_gpu.h>
#endif

namespace caffe2 {

#if !defined(USE_ROCM)
REGISTER_HIP_OPERATOR(
    FileStoreHandlerCreate,
    FileStoreHandlerCreateOp<HIPContext>);
#else
REGISTER_HIP_OPERATOR(
    FileStoreHandlerCreate,
    FileStoreHandlerCreateOp<HIPContext>);
#endif

} // namespace caffe2
