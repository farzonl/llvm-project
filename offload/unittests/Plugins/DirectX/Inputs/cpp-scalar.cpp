// Compile with the matching LLVM build:
// clang --target=dxil-pc-shadermodel6.0-compute -O3 -ffreestanding
//   -fno-exceptions -fno-rtti -emit-llvm -c cpp-scalar.cpp -o cpp-scalar.bc
// opt -dxil-kernel-abi cpp-scalar.bc -o cpp-scalar.kernel.bc
// llc -filetype=obj cpp-scalar.kernel.bc -o cpp-scalar.unsigned.dxil
// dxv -o=cpp-scalar.dxil cpp-scalar.unsigned.dxil

#include <gpuintrin.h>

extern "C" __gpu_kernel __attribute__((reqd_work_group_size(64, 1, 1))) void
scalar(const float *input, float *output, unsigned count, float scale) {
  unsigned i = __gpu_block_id_x() * __gpu_num_threads_x() + __gpu_thread_id_x();
  if (i < count)
    output[i] = input[i] * scale;
}
