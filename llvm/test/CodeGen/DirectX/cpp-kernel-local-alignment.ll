; RUN: opt -S -verify-each -dxil-kernel-abi -dxil-kernel-local-allocations=4294967300,8589934720 %s -o %t.ll
; RUN: FileCheck %s < %t.ll
; RUN: llc -filetype=obj %t.ll -o %t.dxil
; RUN: not opt -disable-output -dxil-kernel-abi -dxil-kernel-local-allocations=4294967300,8589934656 %s 2>&1 | FileCheck %s --check-prefix=SIZE
; RUN: not opt -disable-output -dxil-kernel-abi -dxil-kernel-local-allocations=4294967300,8589967360 %s 2>&1 | FileCheck %s --check-prefix=LIMIT
; RUN: not opt -disable-output -dxil-kernel-abi -dxil-kernel-local-allocations=4294967424,4294967424 %s 2>&1 | FileCheck %s --check-prefix=ALIAS

; CHECK: @sycl.local = internal addrspace(3) global [1 x i32] poison, align 4
; CHECK: @sycl.local.1 = internal addrspace(3) global [32 x i32] poison, align 128
; CHECK: \22alignment\22:128
; SIZE: local allocations require an identity and aligned nonzero bytes
; LIMIT: local allocations exceed the 32768-byte workgroup limit
; ALIAS: aliased local allocation sizes and alignments must match

target datalayout = "e-p:64:64-p3:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr addrspace(3), ptr addrspace(3), ptr }
define void @aligned_local(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %first = load ptr addrspace(3), ptr %args
  %second.addr = getelementptr %Capture, ptr %args, i32 0, i32 1
  %second = load ptr addrspace(3), ptr %second.addr
  %output.addr = getelementptr %Capture, ptr %args, i32 0, i32 2
  %output = load ptr, ptr %output.addr
  store i32 42, ptr addrspace(3) %first, align 4
  store i32 99, ptr addrspace(3) %second, align 128
  %last = getelementptr i32, ptr addrspace(3) %second, i64 31
  store i32 7, ptr addrspace(3) %last, align 4
  call void @llvm.dx.all.memory.barrier.with.group.sync()
  %a = load i32, ptr addrspace(3) %first, align 4
  %b = load i32, ptr addrspace(3) %last, align 4
  %sum = add i32 %a, %b
  store i32 %sum, ptr %output, align 4
  ret void
}
declare void @llvm.dx.all.memory.barrier.with.group.sync()
attributes #0 = { "dx.kernel" "dx.local.specializable" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 24, i32 8, !1, !2, !3}
!1 = !{i32 0, i32 8, i32 2, i32 4}
!2 = !{i32 8, i32 8, i32 2, i32 128}
!3 = !{i32 16, i32 8}
