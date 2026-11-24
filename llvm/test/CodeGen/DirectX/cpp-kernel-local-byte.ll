; RUN: opt -S -verify-each -dxil-kernel-abi %s -o %t.ll
; RUN: FileCheck %s < %t.ll
; RUN: llc -filetype=obj %t.ll -o %t.dxil
; RUN: not opt -disable-output -dxil-kernel-abi -dxil-kernel-local-allocations=4294967300 %s 2>&1 | FileCheck %s --check-prefix=SMALL

; CHECK: @sycl.local = internal addrspace(3) global [2 x i32] poison, align 4
; CHECK: lshr i32 {{.*}}, 2
; CHECK: local.byte.retry:
; CHECK: phi i32
; CHECK: cmpxchg ptr addrspace(3) {{.*}} seq_cst seq_cst, align 4
; CHECK: extractvalue { i32, i1 } {{.*}}, 0
; CHECK: freeze i32
; CHECK: br i1 {{.*}}, label %local.byte.end, label %local.byte.retry, !prof
; CHECK: atomicrmw or ptr addrspace(3) {{.*}}, i32 0 seq_cst, align 4
; CHECK: lshr i32
; CHECK: trunc i32 {{.*}} to i8
; CHECK: \22alignment\22:4
; SMALL: local accessor constant accesses exceed the requested allocation

target datalayout = "e-p:64:64-p3:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr addrspace(3), ptr, i64 }
define void @local_byte(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 !prof !4 {
  %local = load ptr addrspace(3), ptr %args
  %output.addr = getelementptr %Capture, ptr %args, i32 0, i32 1
  %output = load ptr, ptr %output.addr
  %index.addr = getelementptr %Capture, ptr %args, i32 0, i32 2
  %index = load i64, ptr %index.addr
  %dest = getelementptr i8, ptr addrspace(3) %local, i64 %index
  store i8 -91, ptr addrspace(3) %dest, align 1
  %last = getelementptr i8, ptr addrspace(3) %local, i64 4
  store i8 27, ptr addrspace(3) %last, align 1
  call void @llvm.dx.all.memory.barrier.with.group.sync()
  %value = load i8, ptr addrspace(3) %dest, align 1
  store i8 %value, ptr %output, align 1
  ret void
}
declare void @llvm.dx.all.memory.barrier.with.group.sync()
attributes #0 = { "dx.kernel" "dx.local.specializable" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 24, i32 8, !1, !2, !3}
!1 = !{i32 0, i32 8, i32 2, i32 1}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 8}
!4 = !{!"function_entry_count", i64 1}
