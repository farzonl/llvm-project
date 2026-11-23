; RUN: opt -S -verify-each -dxil-kernel-abi %s -o %t.ll
; RUN: FileCheck %s --implicit-check-not="!range" < %t.ll
; RUN: llc -filetype=obj %t.ll -o %t.dxil
; RUN: not opt -disable-output -dxil-kernel-abi -dxil-kernel-local-allocations=4294967304 %s 2>&1 | FileCheck %s --check-prefix=SMALL

; CHECK: @sycl.local = internal addrspace(3) global [4 x i32] poison, align 8
; CHECK-LABEL: define void @local_wide(
; CHECK: trunc i64 {{.*}} to i32
; CHECK: store i32 {{.*}}, ptr addrspace(3) @sycl.local, align 8
; CHECK: lshr i64 {{.*}}, 32
; CHECK: store i32 {{.*}}, ptr addrspace(3) {{.*}}, align 4
; CHECK: call void @llvm.dx.all.memory.barrier.with.group.sync()
; CHECK: load i32, ptr addrspace(3) @sycl.local, align 8
; CHECK: zext i32 {{.*}} to i64
; CHECK: load i32, ptr addrspace(3) {{.*}}, align 4
; CHECK: zext i32 {{.*}} to i64
; CHECK: shl i64 {{.*}}, 32
; CHECK: or i64
; SMALL: local accessor constant accesses exceed the requested allocation

target datalayout = "e-p:64:64-p3:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr addrspace(3), ptr, i64 }
define void @local_wide(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %local = load ptr addrspace(3), ptr %args
  %next = getelementptr i8, ptr addrspace(3) %local, i32 4
  %output.addr = getelementptr %Capture, ptr %args, i32 0, i32 1
  %output = load ptr, ptr %output.addr
  %input.addr = getelementptr %Capture, ptr %args, i32 0, i32 2
  %input = load i64, ptr %input.addr
  store i64 %input, ptr addrspace(3) %local, align 8
  call void @llvm.dx.all.memory.barrier.with.group.sync()
  %first = load i64, ptr addrspace(3) %local, align 8, !range !4
  store i64 %input, ptr addrspace(3) %next, align 4
  call void @llvm.dx.all.memory.barrier.with.group.sync()
  %second = load i64, ptr addrspace(3) %next, align 4
  %sum = add i64 %first, %second
  store i64 %sum, ptr %output
  ret void
}
declare void @llvm.dx.all.memory.barrier.with.group.sync()
attributes #0 = { "dx.kernel" "dx.local.specializable" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 24, i32 8, !1, !2, !3}
!1 = !{i32 0, i32 8, i32 2, i32 8}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 8}
!4 = !{i64 1099511627776, i64 1099511627792}
