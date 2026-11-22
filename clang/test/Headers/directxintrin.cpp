// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -O2 \
// RUN:   -internal-isystem %S/Inputs/include \
// RUN:   -internal-isystem %S/../../lib/Headers -emit-llvm -o - %s | FileCheck %s
// RUN: not %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -O2 \
// RUN:   -internal-isystem %S/Inputs/include \
// RUN:   -internal-isystem %S/../../lib/Headers -DUNSUPPORTED \
// RUN:   -emit-obj -o %t %s 2>&1 | FileCheck %s --check-prefix=ERROR

#include <gpuintrin.h>

extern "C" __gpu_kernel __attribute__((reqd_work_group_size(64, 1, 1)))
void ids(unsigned *out) {
  unsigned i = __gpu_block_id_x() * __gpu_num_threads_x() + __gpu_thread_id_x();
  __gpu_sync_threads();
  out[i] = i;
#ifdef UNSUPPORTED
  out[i] = __gpu_num_blocks_x();
#endif
}

// CHECK-LABEL: define {{.*}}void @ids(
// CHECK: call {{.*}}i32 @llvm.dx.group.id(i32 0)
// CHECK: call {{.*}}i32 @llvm.dx.thread.group.size(i32 0)
// CHECK: call {{.*}}i32 @llvm.dx.thread.id.in.group(i32 0)
// CHECK: call void @llvm.dx.group.memory.barrier.with.group.sync() [ "convergencectrl"(token

#ifdef UNSUPPORTED
// ERROR: GPU intrinsic is not supported by the DirectX C++ ABI
#endif
