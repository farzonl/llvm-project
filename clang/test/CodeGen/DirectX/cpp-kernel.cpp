// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -emit-llvm -O2 -o - %s | FileCheck %s

extern "C" __attribute__((device_kernel, reqd_work_group_size(64, 1, 1)))
void add(const float *a, const float *b, float *c) {
  unsigned i = __builtin_dx_group_id(0) * __builtin_dx_group_size(0) +
               __builtin_dx_thread_id(0);
  c[i] = a[i] + b[i];
}

// CHECK: define {{.*}}void @add(ptr {{[^,]*}}"dx.readonly"{{[^,]*}}, ptr {{[^,]*}}"dx.readonly"{{[^,]*}}, ptr
// CHECK: call i32 @llvm.dx.group.id(i32 0)
// CHECK: call i32 @llvm.dx.thread.group.size(i32 0)
// CHECK: call i32 @llvm.dx.thread.id.in.group(i32 0)
// CHECK: "dx.kernel"
// CHECK: "hlsl.numthreads"="64,1,1"
