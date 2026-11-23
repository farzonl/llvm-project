; RUN: opt -S -dxil-kernel-abi %s | FileCheck %s
; RUN: opt -S -dxil-kernel-abi %s -o %t.ll
; RUN: llc -filetype=obj %t.ll -o %t.dxil

target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"

%Capture = type { float, i32, ptr addrspace(4) }

define void @capture(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %token = call token @llvm.experimental.convergence.entry()
  %base = addrspacecast ptr %args to ptr addrspace(4)
  %scale = load float, ptr addrspace(4) %base
  %cp = getelementptr i8, ptr addrspace(4) %base, i32 4
  %count = load i32, ptr addrspace(4) %cp
  %op = getelementptr i8, ptr addrspace(4) %base, i32 8
  %out = load ptr addrspace(4), ptr addrspace(4) %op
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %active = icmp ult i32 %tid, %count
  br i1 %active, label %work, label %exit
work:
  %dest = getelementptr float, ptr addrspace(4) %out, i32 %tid
  store float %scale, ptr addrspace(4) %dest
  br label %exit
exit:
  ret void
}

declare token @llvm.experimental.convergence.entry()
declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { convergent "dx.kernel" "hlsl.numthreads"="64,1,1" }
!0 = !{i32 16, i32 8, !1, !2, !3}
!1 = !{i32 0, i32 4}
!2 = !{i32 4, i32 4}
!3 = !{i32 8, i32 8}

; CHECK: target datalayout = "{{.*}}p:32:32{{.*}}"
; CHECK: %__dx_kernel_args = type <{ float, i32 }>
; CHECK-LABEL: define void @capture()
; CHECK-NOT: !dx.capture.layout
; CHECK: @llvm.dx.resource.getpointer{{.*}}(target("dx.CBuffer", %__dx_kernel_args) {{.*}}, i32 0) [ "convergencectrl"(token %token) ]
; CHECK: load float, ptr addrspace(2)
; CHECK: load i32, ptr addrspace(2)
; CHECK: store float {{.*}}, ptr %{{.*}}
; CHECK-NOT: addrspace(4)
; CHECK: \22capture\22:{\22offsets\22:[0,4,8],\22pointer_size\22:8,\22size\22:16}
