; RUN: split-file %s %t
; RUN: opt -S -dxil-kernel-abi %t/direct.ll -o %t/direct-abi.ll
; RUN: FileCheck %s --check-prefix=DIRECT < %t/direct-abi.ll
; RUN: llc -filetype=obj %t/direct-abi.ll -o %t/direct.dxil
; RUN: opt -S -dxil-kernel-abi %t/capture.ll -o %t/capture-abi.ll
; RUN: FileCheck %s --check-prefix=CAPTURE < %t/capture-abi.ll
; RUN: llc -filetype=obj %t/capture-abi.ll -o %t/capture.dxil
; RUN: not opt -dxil-kernel-abi %t/local.ll -o %t/out.bc 2>&1 | FileCheck %s --check-prefix=LOCAL
; RUN: not opt -dxil-kernel-abi %t/readonly.ll -o %t/out.bc 2>&1 | FileCheck %s --check-prefix=READONLY
; RUN: not opt -dxil-kernel-abi %t/compare.ll -o %t/out.bc 2>&1 | FileCheck %s --check-prefix=COMPARE

; DIRECT-LABEL: define void @direct()
; DIRECT-NOT: addrspacecast
; DIRECT: getelementptr i8, ptr %{{.*}}, i32 8
; DIRECT: load i32, ptr %
; DIRECT: store i32 %{{.*}}, ptr %
; DIRECT-NOT: addrspacecast
; DIRECT: ret void
; DIRECT: \22kind\22:\22srv\22
; DIRECT: \22kind\22:\22uav\22

; CAPTURE-LABEL: define void @capture()
; CAPTURE-NOT: addrspacecast
; CAPTURE: store i32 37, ptr %
; CAPTURE-NOT: addrspacecast
; CAPTURE: ret void
; CAPTURE-NOT: \22kind\22:\22srv\22
; CAPTURE: \22kind\22:\22uav\22

; LOCAL: buffer address-space casts must preserve global memory
; READONLY: buffer pointer escape or unsupported store
; COMPARE: buffer pointers may only be indexed, loaded, or stored

;--- direct.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @direct(ptr "dx.readonly" %input, ptr %output) #0 {
  %global = addrspacecast ptr %input to ptr addrspace(1)
  %generic = addrspacecast ptr addrspace(1) %global to ptr addrspace(4)
  %element = getelementptr i32, ptr addrspace(4) %generic, i32 2
  %value = load i32, ptr addrspace(4) %element
  %out = addrspacecast ptr %output to ptr addrspace(1)
  store i32 %value, ptr addrspace(1) %out
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }

;--- capture.ll
target datalayout = "e-p:64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr }
define void @capture(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %pointer = load ptr, ptr %args
  %global = addrspacecast ptr %pointer to ptr addrspace(1)
  %element = getelementptr i32, ptr addrspace(1) %global, i32 2
  store i32 37, ptr addrspace(1) %element
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 8, i32 8, !1}
!1 = !{i32 0, i32 8}

;--- local.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @local(ptr %output) #0 {
  %local = addrspacecast ptr %output to ptr addrspace(3)
  store i32 7, ptr addrspace(3) %local
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }

;--- readonly.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @readonly(ptr "dx.readonly" %output) #0 {
  %global = addrspacecast ptr %output to ptr addrspace(1)
  store i32 7, ptr addrspace(1) %global
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }

;--- compare.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @compare(ptr %input, ptr %output) #0 {
  %global = addrspacecast ptr %input to ptr addrspace(1)
  %isnull = icmp eq ptr addrspace(1) %global, null
  %value = zext i1 %isnull to i32
  store i32 %value, ptr %output
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
