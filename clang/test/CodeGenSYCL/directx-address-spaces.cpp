// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -fsycl-is-device -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

using Global = int [[clang::sycl_global]];
using Local = int [[clang::sycl_local]];
using Private = int [[clang::sycl_private]];
using Generic = int [[clang::sycl_generic]];

// CHECK-LABEL: define {{.*}}i32 @read_global(ptr addrspace(1)
extern "C" [[clang::sycl_external]] int read_global(Global *Pointer) {
  return *Pointer;
}
// CHECK-LABEL: define {{.*}}i32 @read_local(ptr addrspace(3)
extern "C" [[clang::sycl_external]] int read_local(Local *Pointer) {
  return *Pointer;
}
// CHECK-LABEL: define {{.*}}i32 @read_private(ptr
// CHECK-SAME: noundef %
extern "C" [[clang::sycl_external]] int read_private(Private *Pointer) {
  return *Pointer;
}
// CHECK-LABEL: define {{.*}}i32 @read_generic(ptr addrspace(4)
extern "C" [[clang::sycl_external]] int read_generic(Generic *Pointer) {
  return *Pointer;
}

// CHECK-LABEL: define {{.*}}void @synchronize()
// CHECK: call void @llvm.dx.group.memory.barrier.with.group.sync()
// CHECK: call void @llvm.dx.all.memory.barrier.with.group.sync()
extern "C" [[clang::sycl_external]] void synchronize() {
  __builtin_dx_sync_threads();
  __builtin_dx_sync_threads_and_memory();
}
