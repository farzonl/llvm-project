// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -O2 \
// RUN:   -internal-isystem %S/../../Headers/Inputs/include \
// RUN:   -internal-isystem %S/../../../lib/Headers \
// RUN:   -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -O2 \
// RUN:   -internal-isystem %S/../../Headers/Inputs/include \
// RUN:   -internal-isystem %S/../../../lib/Headers -emit-obj -o %t %s

#include <gpuintrin.h>

extern __gpu_local float shared[64];

extern "C" __gpu_kernel __attribute__((reqd_work_group_size(64, 1, 1)))
void vector_add(const float *a, const float *b, float *c) {
  unsigned tid = __gpu_thread_id_x();
  unsigned i = __gpu_block_id_x() * __gpu_num_threads_x() + tid;
  shared[tid] = a[i] + b[i];
  __gpu_sync_threads();
  float value = shared[63 - tid];
  __gpu_sync_threads();
  shared[tid] = value;
  __gpu_sync_threads();
  c[i] = shared[63 - tid];
}

// CHECK: @shared = external {{.*}}addrspace(3) global [64 x float]
// CHECK-LABEL: define {{.*}}void @vector_add(
// CHECK: store float {{.*}}, ptr addrspace(3)
// CHECK: call void @llvm.dx.group.memory.barrier.with.group.sync() [ "convergencectrl"(token
// CHECK: load float, ptr addrspace(3)
// CHECK: call void @llvm.dx.group.memory.barrier.with.group.sync() [ "convergencectrl"(token
// CHECK: store float {{.*}}, ptr addrspace(3)
// CHECK: call void @llvm.dx.group.memory.barrier.with.group.sync() [ "convergencectrl"(token
// CHECK: load float, ptr addrspace(3)
