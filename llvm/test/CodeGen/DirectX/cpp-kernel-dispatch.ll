; RUN: opt -S -dxil-kernel-abi %s | FileCheck %s
; RUN: opt -dxil-kernel-abi %s -o %t.bc
; RUN: llc -filetype=obj %t.bc -o %t.dxil
; RUN: llvm-offload-binary -o %t.bin --image=file=%t.dxil,directx-metadata=%t.bc
; RUN: opt -S -dxil-kernel-abi -dxil-kernel-numthreads=3,5,2 %s | FileCheck %s --check-prefix=SPECIAL
; RUN: not opt -dxil-kernel-abi -dxil-kernel-numthreads=33,33,1 %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LIMIT
; RUN: not opt -dxil-kernel-abi -dxil-kernel-numthreads=3,5 %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=DIMS

target triple = "dxil-pc-shadermodel6.0-compute"

define void @geometry(ptr %out, i32 %count) #0 {
  %groups = call i32 @llvm.dx.num.groups(i32 2)
  %local = call i32 @llvm.dx.thread.group.size(i32 1)
  %offset = call i64 @llvm.dx.global.offset(i32 1)
  %hi = lshr i64 %offset, 32
  %word = trunc i64 %hi to i32
  %with_local = add i32 %groups, %local
  %sum = add i32 %with_local, %word
  %value = add i32 %sum, %count
  store i32 %value, ptr %out
  ret void
}

declare i32 @llvm.dx.num.groups(i32)
declare i64 @llvm.dx.global.offset(i32)
declare i32 @llvm.dx.thread.group.size(i32)
attributes #0 = { "dx.kernel" "dx.workgroup.specializable" "hlsl.numthreads"="4,2,2" }

; CHECK: %__dx_kernel_args = type <{ i32, i32, i32, i32, i32, i32, i32, i32, i32, i32 }>
; CHECK-LABEL: define void @geometry()
; CHECK-NOT: call {{.*}}@llvm.dx.num.groups
; CHECK-NOT: call {{.*}}@llvm.dx.global.offset
; CHECK: i32 12)
; CHECK: i32 24)
; CHECK: i32 28)
; CHECK: \22constants\22:{\22register\22:0,\22size\22:40,\22space\22:0}
; CHECK-SAME: \22dispatch\22:{\22offset\22:4,\22version\22:1}
; SPECIAL: %with_local = add i32 {{%.*}}, 5
; SPECIAL: "hlsl.numthreads"="3,5,2"
; SPECIAL: \22numthreads\22:[3,5,2]
; LIMIT: workgroup exceeds 1024 threads
; DIMS: specialization requires three workgroup dimensions
