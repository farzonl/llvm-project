//===-- directxintrin.h - DirectX GPU intrinsic functions
//-------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __DIRECTXINTRIN_H
#define __DIRECTXINTRIN_H

#ifndef __DIRECTX__
#error "This file is intended for DirectX targets"
#endif
#ifndef __GPUINTRIN_H
#error "Never use <directxintrin.h> directly; include <gpuintrin.h> instead"
#endif

#define __gpu_private __attribute__((address_space(0)))
#define __gpu_local __attribute__((address_space(3)))

#define __GPU_DX_DIM(__name, __builtin)                                        \
  _DEFAULT_FN_ATTRS static __inline__ uint32_t __name##_x(void) {              \
    return __builtin(0);                                                       \
  }                                                                            \
  _DEFAULT_FN_ATTRS static __inline__ uint32_t __name##_y(void) {              \
    return __builtin(1);                                                       \
  }                                                                            \
  _DEFAULT_FN_ATTRS static __inline__ uint32_t __name##_z(void) {              \
    return __builtin(2);                                                       \
  }

__GPU_DX_DIM(__gpu_thread_id, __builtin_dx_thread_id)
__GPU_DX_DIM(__gpu_block_id, __builtin_dx_group_id)
__GPU_DX_DIM(__gpu_num_threads, __builtin_dx_group_size)
#undef __GPU_DX_DIM

_DEFAULT_FN_ATTRS static __inline__ void __gpu_sync_threads(void) {
  __builtin_dx_sync_threads();
}

// These operations require additional launch metadata or wave/pointer
// semantics.
#define __GPU_DX_UNSUPPORTED                                                   \
  __attribute__((                                                              \
      error("GPU intrinsic is not supported by the DirectX C++ ABI")))
uint32_t __gpu_num_blocks_x(void) __GPU_DX_UNSUPPORTED;
uint32_t __gpu_num_blocks_y(void) __GPU_DX_UNSUPPORTED;
uint32_t __gpu_num_blocks_z(void) __GPU_DX_UNSUPPORTED;
uint32_t __gpu_num_lanes(void) __GPU_DX_UNSUPPORTED;
uint32_t __gpu_lane_id(void) __GPU_DX_UNSUPPORTED;
uint64_t __gpu_lane_mask(void) __GPU_DX_UNSUPPORTED;
uint32_t __gpu_read_first_lane_u32(uint64_t, uint32_t) __GPU_DX_UNSUPPORTED;
uint64_t __gpu_ballot(uint64_t, bool) __GPU_DX_UNSUPPORTED;
void __gpu_sync_lane(uint64_t) __GPU_DX_UNSUPPORTED;
uint32_t __gpu_shuffle_idx_u32(uint64_t, uint32_t, uint32_t,
                               uint32_t) __GPU_DX_UNSUPPORTED;
bool __gpu_is_ptr_local(const void *) __GPU_DX_UNSUPPORTED;
bool __gpu_is_ptr_private(const void *) __GPU_DX_UNSUPPORTED;
void __gpu_exit(void) __GPU_DX_UNSUPPORTED;
#undef __GPU_DX_UNSUPPORTED

#endif
