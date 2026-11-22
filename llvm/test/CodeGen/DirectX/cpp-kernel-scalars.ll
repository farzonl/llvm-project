; RUN: opt -S -dxil-kernel-abi %s | FileCheck %s
; RUN: llc -filetype=obj %s -o %t.dxil

target triple = "dxil-pc-shadermodel6.0-compute"

define void @scale(ptr "dx.readonly" %in, i32 %count, ptr %out, float %factor) #0 {
  %token = call token @llvm.experimental.convergence.entry()
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %active = icmp ult i32 %tid, %count
  br i1 %active, label %work, label %exit
work:
  %ip = getelementptr float, ptr %in, i32 %tid
  %op = getelementptr float, ptr %out, i32 %tid
  %value = load float, ptr %ip
  %scaled = fmul float %value, %factor
  store float %scaled, ptr %op
  br label %exit
exit:
  ret void
}

declare token @llvm.experimental.convergence.entry()
declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { convergent "dx.kernel" "hlsl.numthreads"="64,1,1" }

; CHECK: %__dx_kernel_args = type <{ i32, float }>
; CHECK-LABEL: define void @scale()
; CHECK: call target("dx.CBuffer", %__dx_kernel_args) @llvm.dx.resource.handlefrombinding
; CHECK: @llvm.dx.resource.getpointer{{.*}}(target("dx.CBuffer", %__dx_kernel_args) {{.*}}, i32 0) [ "convergencectrl"(token %token) ]
; CHECK: load i32, ptr addrspace(2)
; CHECK: @llvm.dx.resource.getpointer{{.*}}(target("dx.CBuffer", %__dx_kernel_args) {{.*}}, i32 4) [ "convergencectrl"(token %token) ]
; CHECK: load float, ptr addrspace(2)
; CHECK: !dx.typeAnnotations
; CHECK: \22index\22:1,\22kind\22:\22scalar\22,\22offset\22:0,\22size\22:4
; CHECK: \22index\22:3,\22kind\22:\22scalar\22,\22offset\22:4,\22size\22:4
; CHECK-SAME: \22constants\22:{\22register\22:0,\22size\22:8,\22space\22:0}
