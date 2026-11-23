; RUN: split-file %s %t
; RUN: opt -S -dxil-kernel-abi %t/zero.ll -o %t/zero-abi.ll
; RUN: FileCheck %s --check-prefix=ZERO < %t/zero-abi.ll
; RUN: llc -filetype=obj %t/zero-abi.ll -o %t/zero.dxil
; RUN: opt -S -dxil-kernel-abi %t/dynamic.ll | FileCheck %s --check-prefix=DYNAMIC
; RUN: opt -S -dxil-kernel-abi %t/capture-copy.ll -o %t/copy-abi.ll
; RUN: FileCheck %s --check-prefix=COPY < %t/copy-abi.ll
; RUN: llc -filetype=obj %t/copy-abi.ll -o %t/copy.dxil
; RUN: opt -S -dxil-kernel-abi %t/mixed-copy.ll | FileCheck %s --check-prefix=MIXED
; RUN: not opt -dxil-kernel-abi %t/partial-copy.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=PARTIAL
; RUN: opt -S -dxil-kernel-abi %t/narrow.ll -o %t/narrow-abi.ll
; RUN: FileCheck %s --check-prefix=NARROW < %t/narrow-abi.ll
; RUN: llc -filetype=obj %t/narrow-abi.ll -o %t/narrow.dxil
; RUN: opt -S -dxil-kernel-abi %t/temporary-copy.ll | FileCheck %s --check-prefix=TEMP
; RUN: opt -S -dxil-kernel-abi %t/indexed-array.ll -o %t/array-abi.ll
; RUN: FileCheck %s --check-prefix=ARRAY < %t/array-abi.ll
; RUN: llc -filetype=obj %t/array-abi.ll -o %t/array.dxil
; RUN: opt -dxil-kernel-abi %t/single-element.ll -o %t/single-abi.bc
; RUN: llc -filetype=obj %t/single-abi.bc -o %t/single.dxil
; RUN: not opt -dxil-kernel-abi %t/indexed-scalar.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=ARRAYERR
; RUN: not opt -dxil-kernel-abi %t/indexed-write.ll -o %t/out.ll 2>&1 | FileCheck %s --check-prefix=ARRAYWRITE

; ZERO-LABEL: define void @zero()
; ZERO: @llvm.dx.resource.getpointer
; ZERO: %[[INDEX:.*]] = phi i64 [ 0,
; ZERO: getelementptr inbounds i8, ptr {{.*}}, i32
; ZERO: store i8 0, ptr
; ZERO: add i64 %[[INDEX]], 1
; ZERO: icmp ult i64 {{.*}}, 24
; ZERO-NOT: call void @llvm.memset

; DYNAMIC-LABEL: define void @dynamic()
; DYNAMIC: @llvm.dx.resource.getpointer
; DYNAMIC: trunc i32 {{.*}} to i8
; DYNAMIC: icmp ne i64 {{.*}}, 0
; DYNAMIC: getelementptr inbounds i8, ptr
; DYNAMIC: store i8
; DYNAMIC-NOT: call void @llvm.memset

; COPY-LABEL: define void @capture_copy()
; COPY: load i64, ptr addrspace(2)
; COPY: load i64, ptr addrspace(2)
; COPY: load i64, ptr addrspace(2)
; COPY: store i64
; COPY: store i64
; COPY: store i64
; COPY-NOT: call void @llvm.memmove
; COPY: \22capture\22:{\22offsets\22:[0,8,16,24]

; MIXED-LABEL: define void @mixed_copy()
; MIXED: load float, ptr addrspace(2)
; MIXED: load i32, ptr addrspace(2)
; MIXED: load i64, ptr addrspace(2)
; MIXED: store float
; MIXED: store i32
; MIXED: store i64
; MIXED-NOT: call void @llvm.memcpy

; PARTIAL: captured aggregate copies must cover complete aligned scalar fields

; NARROW-LABEL: define void @narrow()
; NARROW: load i32, ptr addrspace(2)
; NARROW: load i32, ptr addrspace(2)
; NARROW: load i32, ptr addrspace(2)
; NARROW: trunc i32 {{.*}} to i8
; NARROW: sext i8 {{.*}} to i32
; NARROW: trunc i32 {{.*}} to i8
; NARROW: zext i8 {{.*}} to i32
; NARROW: trunc i32 {{.*}} to i16
; NARROW: sext i16 {{.*}} to i32
; NARROW: \22sizes\22:[8,1,1,2]

; TEMP-LABEL: define void @temporary_copy()
; TEMP: load i64, ptr addrspace(2)
; TEMP: load i64, ptr addrspace(2)
; TEMP: store i64 7, ptr
; TEMP: store i64
; TEMP: store i64
; TEMP-NOT: call void @llvm.memcpy
; TEMP-NOT: alloca
; TEMP: \22offsets\22:[0,16,24]

; ARRAY-LABEL: define void @indexed_array()
; ARRAY: %[[LOCAL:.*]] = alloca [3 x i64]
; ARRAY: store i64
; ARRAY: store i64
; ARRAY: store i64
; ARRAY: %[[ELEMENT:.*]] = getelementptr{{.*}} i8, ptr %[[LOCAL]], i64 %{{.*}}
; ARRAY: load i64, ptr %[[ELEMENT]]
; ARRAY: !{i32 1, !"wchar_size", i32 4}
; ARRAY: \22offsets\22:[0,8,16,24,32]
; ARRAYERR: dynamic capture indexing requires a fixed-size scalar array
; ARRAYWRITE: dynamic capture arrays may only be indexed and read

;--- zero.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @zero(ptr %out) #0 {
  call void @llvm.memset.p0.i64(ptr align 8 %out, i8 0, i64 24, i1 false)
  ret void
}
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }

;--- capture-copy.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr, [3 x i64] }
define void @capture_copy(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %out = load ptr, ptr %args
  %source = getelementptr %Capture, ptr %args, i32 0, i32 1
  call void @llvm.memmove.p0.p0.i64(ptr align 8 %out, ptr align 8 %source,
                                    i64 24, i1 false)
  ret void
}
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1 immarg)
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 32, i32 8, !1, !2, !3, !4}
!1 = !{i32 0, i32 8, i32 1}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 8}
!4 = !{i32 24, i32 8}

;--- indexed-array.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr, [3 x i64], i32 }
define void @indexed_array(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %base = getelementptr i8, ptr %args, i64 8
  %ip = getelementptr i8, ptr %args, i64 32
  %index = load i32, ptr %ip
  %wide = sext i32 %index to i64
  %element = getelementptr inbounds [8 x i8], ptr %base, i64 %wide
  %value = load i64, ptr %element
  %out = load ptr, ptr %args
  store i64 %value, ptr %out
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 40, i32 8, !1, !2, !3, !4, !5}
!1 = !{i32 0, i32 8, i32 1}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 8}
!4 = !{i32 24, i32 8}
!5 = !{i32 32, i32 4}
!llvm.module.flags = !{!6}
!6 = !{i32 1, !"wchar_size", i32 2}

;--- narrow.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr, i8, i8, i16 }
define void @narrow(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %out = load ptr, ptr %args
  %ap = getelementptr i8, ptr %args, i64 8
  %a = load i8, ptr %ap
  %signed = sext i8 %a to i32
  %bp = getelementptr i8, ptr %args, i64 9
  %b = load i8, ptr %bp
  %unsigned = zext i8 %b to i32
  %sp = getelementptr i8, ptr %args, i64 10
  %s = load i16, ptr %sp
  %short = sext i16 %s to i32
  store i32 %signed, ptr %out
  %out1 = getelementptr i32, ptr %out, i32 1
  store i32 %unsigned, ptr %out1
  %out2 = getelementptr i32, ptr %out, i32 2
  store i32 %short, ptr %out2
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 16, i32 8, !1, !2, !3, !4}
!1 = !{i32 0, i32 8, i32 1}
!2 = !{i32 8, i32 1}
!3 = !{i32 9, i32 1}
!4 = !{i32 10, i32 2}

;--- mixed-copy.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr, { float, i32, i64 } }
define void @mixed_copy(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %out = load ptr, ptr %args
  %source = getelementptr %Capture, ptr %args, i32 0, i32 1
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %out, ptr align 8 %source,
                                   i64 16, i1 false)
  ret void
}
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 24, i32 8, !1, !2, !3, !4}
!1 = !{i32 0, i32 8, i32 1}
!2 = !{i32 8, i32 4}
!3 = !{i32 12, i32 4}
!4 = !{i32 16, i32 8}

;--- partial-copy.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr, [2 x i64] }
define void @partial_copy(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %out = load ptr, ptr %args
  %source = getelementptr i8, ptr %args, i64 9
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %out, ptr %source, i64 8, i1 false)
  ret void
}
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 24, i32 8, !1, !2, !3}
!1 = !{i32 0, i32 8, i32 1}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 8}

;--- dynamic.ll
target triple = "dxil-pc-shadermodel6.0-compute"
define void @dynamic(ptr %out, i32 %value, i32 %length) #0 {
  %byte = trunc i32 %value to i8
  %size = zext i32 %length to i64
  call void @llvm.memset.p0.i64(ptr %out, i8 %byte, i64 %size, i1 false)
  ret void
}
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }

;--- temporary-copy.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr, [3 x i64] }
define void @temporary_copy(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %temporary = alloca [2 x i64], align 8
  %source = getelementptr i8, ptr %args, i64 16
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %temporary, ptr align 8 %source,
                                   i64 16, i1 false)
  %out = load ptr, ptr %args
  store i64 7, ptr %out
  %dest = getelementptr i8, ptr %out, i64 8
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %dest, ptr align 8 %temporary,
                                   i64 16, i1 false)
  ret void
}
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 32, i32 8, !1, !2, !3, !4}
!1 = !{i32 0, i32 8, i32 1}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 8}
!4 = !{i32 24, i32 8}

;--- single-element.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr, [1 x i64], i32 }
define void @single_element(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %base = getelementptr i8, ptr %args, i64 8
  %ip = getelementptr i8, ptr %args, i64 16
  %index = load i32, ptr %ip
  %wide = sext i32 %index to i64
  %element = getelementptr [8 x i8], ptr %base, i64 %wide
  %value = load i64, ptr %element
  %out = load ptr, ptr %args
  store i64 %value, ptr %out
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 24, i32 8, !1, !2, !3}
!1 = !{i32 0, i32 8, i32 1}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 4}

;--- indexed-scalar.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr, i64, i32 }
define void @indexed_scalar(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %base = getelementptr i8, ptr %args, i64 8
  %ip = getelementptr i8, ptr %args, i64 16
  %index = load i32, ptr %ip
  %wide = sext i32 %index to i64
  %element = getelementptr [8 x i8], ptr %base, i64 %wide
  %value = load i64, ptr %element
  %out = load ptr, ptr %args
  store i64 %value, ptr %out
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 24, i32 8, !1, !2, !3}
!1 = !{i32 0, i32 8, i32 1}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 4}

;--- indexed-write.ll
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { [3 x i64], i32 }
define void @indexed_write(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  %ip = getelementptr i8, ptr %args, i64 24
  %index = load i32, ptr %ip
  %wide = sext i32 %index to i64
  %element = getelementptr [8 x i8], ptr %args, i64 %wide
  store i64 7, ptr %element
  ret void
}
attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
!0 = !{i32 32, i32 8, !1, !2, !3, !4}
!1 = !{i32 0, i32 8}
!2 = !{i32 8, i32 8}
!3 = !{i32 16, i32 8}
!4 = !{i32 24, i32 4}
