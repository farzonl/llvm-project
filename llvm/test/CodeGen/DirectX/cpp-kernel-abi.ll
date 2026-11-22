; RUN: opt -S -dxil-kernel-abi %s | FileCheck %s

target triple = "dxil-pc-shadermodel6.0-compute"

define void @add(ptr "dx.readonly" %a, ptr "dx.readonly" %b, ptr %c) #0 {
  %token = call token @llvm.experimental.convergence.entry()
  %group = call i32 @llvm.dx.group.id(i32 0)
  %size = call i32 @llvm.dx.thread.group.size(i32 0)
  %thread = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %base = mul i32 %group, %size
  %i = add i32 %base, %thread
  %ap = getelementptr float, ptr %a, i32 %i
  %bp = getelementptr float, ptr %b, i32 %i
  %cp = getelementptr float, ptr %c, i32 %i
  %av = load float, ptr %ap
  %bv = load float, ptr %bp
  %sum = fadd float %av, %bv
  call void @llvm.dx.group.memory.barrier.with.group.sync() [ "convergencectrl"(token %token) ]
  store float %sum, ptr %cp
  ret void
}

declare i32 @llvm.dx.group.id(i32)
declare i32 @llvm.dx.thread.id.in.group(i32)
declare i32 @llvm.dx.thread.group.size(i32)
declare token @llvm.experimental.convergence.entry()
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { convergent "dx.kernel" "hlsl.numthreads"="64,1,1" }

; CHECK-LABEL: define void @add()
; CHECK: %token = call token @llvm.experimental.convergence.entry()
; CHECK: @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
; CHECK: @llvm.dx.resource.getpointer{{.*}} [ "convergencectrl"(token %token) ]
; CHECK: @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 1, i32 1, i32 0, ptr null)
; CHECK: @llvm.dx.resource.handlefrombinding{{.*}}tdx.RawBuffer_i8_1_0t(i32 0, i32 0, i32 1, i32 0, ptr null)
; CHECK: mul i32 %group, 64
; CHECK-NOT: call i32 @llvm.dx.thread.group.size
; CHECK: "hlsl.shader"="compute"
; CHECK: !llvm.offload.manifest
; CHECK: \22entry\22:\22add\22
