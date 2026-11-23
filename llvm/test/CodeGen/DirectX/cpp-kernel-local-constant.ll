; RUN: opt -S -dxil-kernel-abi %s -o %t.ll
; RUN: FileCheck %s < %t.ll
; RUN: llc -filetype=obj %t.ll -o %t.dxil
; RUN: opt -S -dxil-kernel-abi -dxil-kernel-local-allocations=4294969344 %s | FileCheck %s --check-prefix=SPECIALIZED
; RUN: not opt -disable-output -dxil-kernel-abi -dxil-kernel-local-allocations=4294968320 %s 2>&1 | FileCheck %s --check-prefix=SMALL

; CHECK: @sycl.local = internal addrspace(3) global [257 x i32] poison, align 4
; SPECIALIZED: @sycl.local = internal addrspace(3) global [512 x i32] poison, align 4
; SMALL: local accessor constant accesses exceed the requested allocation

target datalayout = "e-p:64:64-p3:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr addrspace(3), ptr }
define void @local_constant(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %local = load ptr addrspace(3), ptr %args
  %sample = getelementptr i32, ptr addrspace(3) %local, i32 256
  %output.addr = getelementptr %Capture, ptr %args, i32 0, i32 1
  %output = load ptr, ptr %output.addr
  store i32 43, ptr addrspace(3) %sample
  call void @llvm.dx.all.memory.barrier.with.group.sync()
  %value = load i32, ptr addrspace(3) %sample
  store i32 %value, ptr %output
  ret void
}
declare void @llvm.dx.all.memory.barrier.with.group.sync()
attributes #0 = { "dx.kernel" "dx.local.specializable" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 16, i32 8, !1, !2}
!1 = !{i32 0, i32 8, i32 2, i32 4}
!2 = !{i32 8, i32 8}
