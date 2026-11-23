; RUN: opt -S -dxil-kernel-abi %s | FileCheck %s
; RUN: llc -filetype=obj %s -o %t.dxil

target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"

%Capture = type { i32, [2 x [2 x i64]], ptr }

define void @capture_array(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %bias = load i32, ptr %args
  %first = getelementptr %Capture, ptr %args, i32 0, i32 1, i32 0, i32 0
  %a = load i64, ptr %first
  %last = getelementptr %Capture, ptr %args, i32 0, i32 1, i32 1, i32 1
  %b = load i64, ptr %last
  %op = getelementptr %Capture, ptr %args, i32 0, i32 2
  %out = load ptr, ptr %op
  %sum = add i64 %a, %b
  %low = trunc i64 %sum to i32
  %result = add i32 %low, %bias
  %groups = call i32 @llvm.dx.num.groups(i32 0)
  %with_groups = add i32 %result, %groups
  store i32 %with_groups, ptr %out
  ret void
}

declare i32 @llvm.dx.num.groups(i32)
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 48, i32 8, !1, !2, !3, !4, !5, !6}
!1 = !{i32 0, i32 4}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 8}
!4 = !{i32 24, i32 8}
!5 = !{i32 32, i32 8}
!6 = !{i32 40, i32 8}

; CHECK: %__dx_kernel_args = type <{ i32, i64, i64, i32
; CHECK-LABEL: define void @capture_array()
; CHECK: load i32, ptr addrspace(2)
; CHECK: @llvm.dx.resource.getpointer{{.*}}, i32 8)
; CHECK: load i64, ptr addrspace(2)
; CHECK: @llvm.dx.resource.getpointer{{.*}}, i32 16)
; CHECK: load i64, ptr addrspace(2)
; CHECK: \22capture\22:{\22offsets\22:[0,8,32,40],\22pointer_size\22:8,\22size\22:48}
; CHECK-SAME: \22constants\22:{\22register\22:0,\22size\22:60,\22space\22:0}
; CHECK-SAME: \22dispatch\22:{\22offset\22:24,\22version\22:1}
