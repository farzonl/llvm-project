; RUN: split-file %s %t
; RUN: not opt -S -dxil-kernel-abi %t/scalar.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=SCALAR
; RUN: not opt -S -dxil-kernel-abi %t/escape.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=ESCAPE
; RUN: not opt -S -dxil-kernel-abi %t/dimensions.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=DIMS
; RUN: not opt -S -dxil-kernel-abi %t/multiple.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=MULTIPLE

; SCALAR: only default-address-space buffer pointer arguments are supported
; ESCAPE: buffer pointer escape or unsupported store
; DIMS: reqd_work_group_size is required
; MULTIPLE: exactly one defined kernel is required

;--- scalar.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @scalar(i32 %value) #0 {
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="64,1,1" }

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
