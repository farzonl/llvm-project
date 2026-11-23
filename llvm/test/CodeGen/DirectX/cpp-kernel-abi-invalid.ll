; RUN: split-file %s %t
; RUN: not opt -S -dxil-kernel-abi %t/scalar.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=SCALAR
; RUN: not opt -S -dxil-kernel-abi %t/escape.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not opt -S -dxil-kernel-abi %t/dimensions.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=DIMS
; RUN: not opt -S -dxil-kernel-abi %t/multiple.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=MULTIPLE
; RUN: not opt -S -dxil-kernel-abi %t/dtor.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=DTOR
; RUN: not opt -dxil-kernel-abi -dxil-kernel-numthreads=2,1,1 %t/escape.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=FIXED
; RUN: not opt -S -dxil-kernel-abi %t/readonly-memset.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not opt -S -dxil-kernel-abi %t/volatile-memset.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=ESCAPE

; SCALAR: arguments must be default-address-space buffer pointers or 32/64-bit integer or float scalars
; ESCAPE: buffer pointer escape or unsupported store
; DIMS: reqd_work_group_size is required
; MULTIPLE: exactly one defined kernel is required
; DTOR: dynamic device initialization and destruction are not supported
; FIXED: kernel has a fixed workgroup size

;--- scalar.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @scalar(i16 %value) #0 {
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="64,1,1" }

;--- readonly-memset.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @readonly_memset(ptr "dx.readonly" %out) #0 {
  call void @llvm.memset.p0.i64(ptr %out, i8 0, i64 24, i1 false)
  ret void
}
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }

;--- volatile-memset.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @volatile_memset(ptr %out) #0 {
  call void @llvm.memset.p0.i64(ptr %out, i8 0, i64 24, i1 true)
  ret void
}
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }

;--- dtor.ll
target triple = "dxil-pc-shadermodel6.0-compute"
@llvm.global_dtors = appending global [1 x { i32, ptr, ptr }] [
  { i32, ptr, ptr } { i32 65535, ptr @destroy, ptr null }]
declare void @destroy()
define void @dtor_kernel() #0 {
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }

;--- escape.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @escape(ptr %value, ptr %out) #0 {
  store ptr %value, ptr %out
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="64,1,1" }

;--- dimensions.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @dimensions() #0 {
  ret void
}
attributes #0 = { "dx.kernel" }

;--- multiple.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @a() #0 {
  ret void
}
define void @b() #0 {
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="64,1,1" }
