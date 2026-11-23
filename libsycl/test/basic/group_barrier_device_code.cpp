//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// RUN: %clangxx -fsycl -fsycl-device-only -S -emit-llvm %s -o - | FileCheck %s
// RUN: %clangxx -fsycl -fsycl-device-only --offload-targets=dxil-pc-shadermodel6.0-compute -O2 -S -emit-llvm -DTEST_DIRECTX %s -o - | FileCheck %s --check-prefix=DXCHECK
// RUN: %clangxx -fsycl -fsycl-device-only --offload-targets=dxil-pc-shadermodel6.0-compute -O2 -S -emit-llvm -DTEST_DIRECTX -DTEST_DEVICE_SCOPE %s -o - | FileCheck %s --check-prefix=DXERROR

// Checks the execution scope, memory scope and memory semantics operands that
// group_barrier passes to __spirv_ControlBarrier.

#include <sycl/sycl.hpp>

#ifdef TEST_DIRECTX
// DXCHECK-COUNT-3: call void @llvm.dx.all.memory.barrier.with.group.sync()
// DXCHECK-NOT: __spirv_ControlBarrier
// DXERROR: call void {{.*}}unsupportedDirectXBarrierScope
// DXERROR: "dontcall-error"="DirectX SYCL barriers require work-group execution and work-item or work-group fence scope"
void testDirectX(sycl::queue Q, int *Output) {
  Q.parallel_for<class DirectXBarrierKernel>(
      sycl::nd_range<1>{8, 4}, [=](sycl::nd_item<1> It) {
        Output[It.get_global_linear_id()] = 1;
        sycl::group_barrier(It.get_group());
        sycl::group_barrier(It.get_group(), sycl::memory_scope::work_item);
        sycl::group_barrier(It.get_group(), sycl::memory_scope::work_group);
#  ifdef TEST_DEVICE_SCOPE
        sycl::group_barrier(It.get_group(), sycl::memory_scope::device);
#  endif
      });
}
#else
void test(sycl::queue Q) {
  Q.parallel_for<class BarrierKernel>(
      sycl::nd_range<1>{8, 4}, [=](sycl::nd_item<1> It) {
        // Execution scope is Workgroup (2), memory scope defaults to the
        // group's fence_scope, i.e. Workgroup (2).
        // CHECK: call spir_func void @_Z22__spirv_ControlBarrierjjj(i32 noundef 2, i32 noundef 2, i32 noundef 400)
        sycl::group_barrier(It.get_group());
        // CHECK: call spir_func void @_Z22__spirv_ControlBarrierjjj(i32 noundef 2, i32 noundef 4, i32 noundef 400)
        sycl::group_barrier(It.get_group(), sycl::memory_scope::work_item);
        // CHECK: call spir_func void @_Z22__spirv_ControlBarrierjjj(i32 noundef 2, i32 noundef 3, i32 noundef 400)
        sycl::group_barrier(It.get_group(), sycl::memory_scope::sub_group);
        // CHECK: call spir_func void @_Z22__spirv_ControlBarrierjjj(i32 noundef 2, i32 noundef 2, i32 noundef 400)
        sycl::group_barrier(It.get_group(), sycl::memory_scope::work_group);
        // CHECK: call spir_func void @_Z22__spirv_ControlBarrierjjj(i32 noundef 2, i32 noundef 1, i32 noundef 400)
        sycl::group_barrier(It.get_group(), sycl::memory_scope::device);
        // CHECK: call spir_func void @_Z22__spirv_ControlBarrierjjj(i32 noundef 2, i32 noundef 0, i32 noundef 400)
        sycl::group_barrier(It.get_group(), sycl::memory_scope::system);

        // Execution scope is Subgroup (3), memory scope defaults to the
        // sub-group's fence_scope, i.e. Subgroup (3).
        // CHECK: call spir_func void @_Z22__spirv_ControlBarrierjjj(i32 noundef 3, i32 noundef 3, i32 noundef 400)
        sycl::group_barrier(It.get_sub_group());
      });
}
#endif
