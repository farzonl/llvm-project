; RUN: opt -S -dxil-kernel-abi %s -o %t.ll
; RUN: FileCheck %s < %t.ll
; RUN: llc -filetype=obj %t.ll -o %t.dxil

; CHECK: @sycl.local = internal addrspace(3) global [1 x i32] poison, align 4
; CHECK-LABEL: define void @local_float()
; CHECK: store i32 1069547520, ptr addrspace(3) @sycl.local
; CHECK: call void @llvm.dx.all.memory.barrier.with.group.sync()
; CHECK: %[[BITS:.*]] = load i32, ptr addrspace(3) @sycl.local
; CHECK: bitcast i32 %[[BITS]] to float

target datalayout = "e-p:64:64-p3:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr addrspace(3), ptr }
define void @local_float(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %local = load ptr addrspace(3), ptr %args
  %output.addr = getelementptr %Capture, ptr %args, i32 0, i32 1
  %output = load ptr, ptr %output.addr
  store float 1.5, ptr addrspace(3) %local
  call void @llvm.dx.all.memory.barrier.with.group.sync()
  %value = load float, ptr addrspace(3) %local
  store float %value, ptr %output
  ret void
}
declare void @llvm.dx.all.memory.barrier.with.group.sync()
attributes #0 = { "dx.kernel" "dx.local.specializable" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 16, i32 8, !1, !2}
!1 = !{i32 0, i32 8, i32 2, i32 4}
!2 = !{i32 8, i32 8}
