// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gpuintrin.h>

extern "C" __gpu_kernel __attribute__((reqd_work_group_size(64, 1, 1))) void
vector_add(const float *A, const float *B, float *C) {
  unsigned I = __gpu_block_id(0) * __gpu_num_threads(0) + __gpu_thread_id(0);
  C[I] = A[I] + B[I];
}
