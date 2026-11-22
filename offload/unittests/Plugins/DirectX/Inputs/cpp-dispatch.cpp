// Generate cpp-dispatch-{1,2,3}.bin with -DDISPATCH_DIMS={1,2,3}.
// clang --target=dxil-pc-shadermodel6.0-compute -std=c++17 -O1
// -emit-llvm -c cpp-dispatch.cpp -o input.bc
// opt -dxil-kernel-abi input.bc -o kernel.bc
// llc -filetype=obj kernel.bc -o kernel.dxil
// dxv -o=signed.dxil kernel.dxil
// llvm-offload-binary --image=file=signed.dxil,directx-metadata=kernel.bc
// -o cpp-dispatch-{1,2,3}.bin

#ifndef DISPATCH_DIMS
#error DISPATCH_DIMS must select 1, 2, or 3 work dimensions
#endif

__attribute__((device_kernel,
               reqd_work_group_size(4, DISPATCH_DIMS > 1 ? 2 : 1,
                                    DISPATCH_DIMS > 2 ? 2 : 1))) void
geometry(unsigned *out) {
  unsigned x = __builtin_dx_group_id(0) * __builtin_dx_group_size(0) +
               __builtin_dx_thread_id(0);
  unsigned y = __builtin_dx_group_id(1) * __builtin_dx_group_size(1) +
               __builtin_dx_thread_id(1);
  unsigned z = __builtin_dx_group_id(2) * __builtin_dx_group_size(2) +
               __builtin_dx_thread_id(2);
  unsigned nx = __builtin_dx_num_groups(0);
  unsigned ny = __builtin_dx_num_groups(1);
  unsigned nz = __builtin_dx_num_groups(2);
  unsigned index = (z * ny * __builtin_dx_group_size(1) + y) * nx *
                       __builtin_dx_group_size(0) +
                   x;
  unsigned *record = out + index * 24;
  unsigned long long ox = __builtin_dx_global_offset(0);
  unsigned long long oy = __builtin_dx_global_offset(1);
  unsigned long long oz = __builtin_dx_global_offset(2);
  unsigned long long gx = ox + x, gy = oy + y, gz = oz + z;
  record[0] = static_cast<unsigned>(gx);
  record[1] = gx >> 32;
  record[2] = static_cast<unsigned>(gy);
  record[3] = gy >> 32;
  record[4] = static_cast<unsigned>(gz);
  record[5] = gz >> 32;
  record[6] = __builtin_dx_thread_id(0);
  record[7] = __builtin_dx_thread_id(1);
  record[8] = __builtin_dx_thread_id(2);
  record[9] = __builtin_dx_group_id(0);
  record[10] = __builtin_dx_group_id(1);
  record[11] = __builtin_dx_group_id(2);
  record[12] = nx;
  record[13] = ny;
  record[14] = nz;
  record[15] = nx * __builtin_dx_group_size(0);
  record[16] = ny * __builtin_dx_group_size(1);
  record[17] = nz * __builtin_dx_group_size(2);
  record[18] = static_cast<unsigned>(ox);
  record[19] = ox >> 32;
  record[20] = static_cast<unsigned>(oy);
  record[21] = oy >> 32;
  record[22] = static_cast<unsigned>(oz);
  record[23] = oz >> 32;
}
