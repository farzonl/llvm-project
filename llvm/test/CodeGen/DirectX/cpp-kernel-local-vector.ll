; RUN: split-file %s %t
; RUN: opt -S -verify-each -dxil-kernel-abi %t/float.ll -o %t/float-out.ll
; RUN: FileCheck %s --check-prefix=FLOAT < %t/float-out.ll
; RUN: llc -filetype=obj %t/float-out.ll -o %t/float.dxil
; RUN: opt -S -verify-each -dxil-kernel-abi %t/copy.ll -o %t/copy-out.ll
; RUN: FileCheck %s --check-prefix=COPY < %t/copy-out.ll
; RUN: llc -filetype=obj %t/copy-out.ll -o %t/copy.dxil
; RUN: opt -S -verify-each -dxil-kernel-abi %t/wide.ll -o %t/wide-out.ll
; RUN: FileCheck %s --check-prefix=WIDE < %t/wide-out.ll
; RUN: llc -filetype=obj %t/wide-out.ll -o %t/wide.dxil
; RUN: not opt -disable-output -dxil-kernel-abi -dxil-kernel-local-allocations=4294967312 %t/float.ll 2>&1 | FileCheck %s --check-prefix=SMALL
; RUN: not opt -disable-output -dxil-kernel-abi %t/partial.ll 2>&1 | FileCheck %s --check-prefix=PARTIAL
; RUN: not opt -disable-output -dxil-kernel-abi %t/volatile.ll 2>&1 | FileCheck %s --check-prefix=VOLATILE

; FLOAT: @sycl.local = internal addrspace(3) global [8 x i32] poison, align 16
; FLOAT-LABEL: define void @local_vector(
; FLOAT-NOT: load <4 x float>
; FLOAT-NOT: store <4 x float>
; FLOAT: load i32, ptr addrspace(3)
; FLOAT: bitcast i32 {{.*}} to float
; FLOAT: \22alignment\22:16
; COPY-LABEL: define void @local_copy(
; COPY: load i32, ptr addrspace(3) {{.*}}, align 4
; COPY: load i32, ptr addrspace(3) {{.*}}, align 4
; COPY: load i32, ptr addrspace(3) {{.*}}, align 4
; COPY: store i32
; COPY: store i32
; COPY: store i32
; COPY-NOT: call void @llvm.memmove
; WIDE-LABEL: define void @local_wide_vector(
; WIDE: trunc i64 {{.*}} to i32
; WIDE: lshr i64 {{.*}}, 32
; WIDE: load i32, ptr addrspace(3)
; WIDE: zext i32 {{.*}} to i64
; SMALL: local accessor constant accesses exceed the requested allocation
; PARTIAL: memory copies require a constant word-aligned size
; VOLATILE: local accessors require non-atomic 32/64-bit integer or 32-bit float scalar or fixed-vector accesses

;--- float.ll
target datalayout = "e-p:64:64-p3:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr addrspace(3), ptr, i64 }
define void @local_vector(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %local = load ptr addrspace(3), ptr %args
  %output.addr = getelementptr %Capture, ptr %args, i32 0, i32 1
  %output = load ptr, ptr %output.addr
  %index.addr = getelementptr %Capture, ptr %args, i32 0, i32 2
  %index = load i64, ptr %index.addr
  %values = load <4 x float>, ptr %output, align 16
  %dest = getelementptr <4 x float>, ptr addrspace(3) %local, i64 %index
  store <4 x float> %values, ptr addrspace(3) %dest, align 16
  %next = getelementptr <4 x float>, ptr addrspace(3) %local, i64 1
  store <4 x float> %values, ptr addrspace(3) %next, align 16
  call void @llvm.dx.all.memory.barrier.with.group.sync()
  %result = load <4 x float>, ptr addrspace(3) %dest, align 16
  store <4 x float> %result, ptr %output, align 16
  ret void
}
declare void @llvm.dx.all.memory.barrier.with.group.sync()
attributes #0 = { "dx.kernel" "dx.local.specializable" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 24, i32 8, !1, !2, !3}
!1 = !{i32 0, i32 8, i32 2, i32 16}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 8}

;--- partial.ll
target datalayout = "e-p:64:64-p3:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr addrspace(3) }
define void @partial_copy(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %local = load ptr addrspace(3), ptr %args
  %src = getelementptr i8, ptr addrspace(3) %local, i64 4
  call void @llvm.memcpy.p3.p3.i64(ptr addrspace(3) align 4 %local,
                                 ptr addrspace(3) align 4 %src, i64 3, i1 false)
  ret void
}
declare void @llvm.memcpy.p3.p3.i64(ptr addrspace(3), ptr addrspace(3), i64, i1 immarg)
attributes #0 = { "dx.kernel" "dx.local.specializable" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 8, i32 8, !1}
!1 = !{i32 0, i32 8, i32 2, i32 4}

;--- volatile.ll
target datalayout = "e-p:64:64-p3:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr addrspace(3) }
define void @volatile_vector(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %local = load ptr addrspace(3), ptr %args
  %value = load volatile <4 x float>, ptr addrspace(3) %local, align 16
  ret void
}
attributes #0 = { "dx.kernel" "dx.local.specializable" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 8, i32 8, !1}
!1 = !{i32 0, i32 8, i32 2, i32 16}

;--- copy.ll
target datalayout = "e-p:64:64-p3:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr addrspace(3), ptr, i64 }
define void @local_copy(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %local = load ptr addrspace(3), ptr %args
  %output.addr = getelementptr %Capture, ptr %args, i32 0, i32 1
  %output = load ptr, ptr %output.addr
  %src = getelementptr i8, ptr addrspace(3) %local, i64 4
  call void @llvm.memmove.p3.p3.i64(ptr addrspace(3) align 16 %local,
                                  ptr addrspace(3) align 4 %src, i64 12, i1 false)
  %result = load <4 x float>, ptr addrspace(3) %local, align 16
  store <4 x float> %result, ptr %output, align 16
  ret void
}
declare void @llvm.memmove.p3.p3.i64(ptr addrspace(3), ptr addrspace(3), i64, i1 immarg)
attributes #0 = { "dx.kernel" "dx.local.specializable" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 24, i32 8, !1, !2, !3}
!1 = !{i32 0, i32 8, i32 2, i32 16}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 8}

;--- wide.ll
target datalayout = "e-p:64:64-p3:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr addrspace(3), ptr, i64 }
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)
define void @local_wide_vector(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %local = load ptr addrspace(3), ptr %args
  %output.addr = getelementptr %Capture, ptr %args, i32 0, i32 1
  %output = load ptr, ptr %output.addr
  %global = addrspacecast ptr %output to ptr addrspace(1)
  %raw.global = addrspacecast ptr addrspace(1) %global to ptr
  %raw.local = addrspacecast ptr addrspace(3) %local to ptr
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %raw.local,
                                 ptr align 16 %raw.global, i64 16, i1 false)
  %values = load <2 x i64>, ptr addrspace(3) %local, align 16
  store <2 x i64> %values, ptr addrspace(3) %local, align 16
  call void @llvm.dx.all.memory.barrier.with.group.sync()
  %result = load <2 x i64>, ptr addrspace(3) %local, align 16
  store <2 x i64> %result, ptr %output, align 16
  ret void
}
declare void @llvm.dx.all.memory.barrier.with.group.sync()
attributes #0 = { "dx.kernel" "dx.local.specializable" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 24, i32 8, !1, !2, !3}
!1 = !{i32 0, i32 8, i32 2, i32 16}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 8}
