; RUN: split-file %s %t
; RUN: opt -S -verify-each -dxil-kernel-abi %t/empty.ll -o %t/out.ll
; RUN: FileCheck %s < %t/out.ll
; RUN: llc -filetype=obj %t/out.ll -o %t/out.dxil
; RUN: not opt -disable-output -dxil-kernel-abi %t/read-padding.ll 2>&1 | FileCheck %s --check-prefix=READ
; RUN: not opt -disable-output -dxil-kernel-abi %t/bad-size.ll 2>&1 | FileCheck %s --check-prefix=SIZE

; CHECK-LABEL: define void @empty()
; CHECK-NOT: byval
; CHECK: ret void
; CHECK: \22capture\22:{\22offsets\22:[],\22pointer_size\22:8,\22size\22:1}
; READ: capture load does not match a complete record field
; SIZE: capture ABI requires a 64-bit pointer layout and a bounded record

;--- empty.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { i8 }
define void @empty(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 1, i32 8}

;--- read-padding.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { i8 }
define void @read_padding(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %padding = load i8, ptr %args
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 1, i32 8}

;--- bad-size.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { i8 }
define void @bad_size(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 2, i32 8}
