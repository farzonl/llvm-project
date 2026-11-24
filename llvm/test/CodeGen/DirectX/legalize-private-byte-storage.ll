; RUN: split-file %s %t
; RUN: opt -S -passes=dxil-legalize -verify-each %t/source.ll | FileCheck %s
; RUN: opt -S -passes=dxil-legalize -verify-each %t/native.ll | FileCheck %s --check-prefix=NATIVE
; RUN: opt -S -dxil-kernel-abi -verify-each %t/source.ll -o %t.abi.ll
; RUN: FileCheck %s < %t.abi.ll --check-prefix=ABI
; RUN: opt -strip-debug %t.abi.ll -o %t.emit.bc
; RUN: llc -filetype=obj %t.emit.bc -o %t.dxil
; RUN: not opt -S -passes=dxil-legalize -verify-each %t/unsupported.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERROR

;--- source.ll
target datalayout = "e-p:64:64"
target triple = "dxil-pc-shadermodel6.0-compute"

%Wrapper = type { ptr }

define void @compare_pointer_bytes(ptr %out) #0 !dbg !3 {
entry:
  %storage = alloca ptr, align 8
    #dbg_declare(ptr %storage, !6, !DIExpression(), !7)
  store ptr inttoptr (i64 72623859790382856 to ptr), ptr %storage, align 8
  br label %loop

loop:
  %address = phi ptr [ %storage, %entry ], [ %next, %loop ]
  %count = phi i32 [ 8, %entry ], [ %remaining, %loop ]
  %byte = load i8, ptr %address, align 1
  %expected = trunc i32 %count to i8
  %equal = icmp eq i8 %byte, %expected
  %next = getelementptr i8, ptr %address, i32 1
  %remaining = add i32 %count, -1
  %again = icmp ne i32 %remaining, 0
  %continue = and i1 %again, %equal
  br i1 %continue, label %loop, label %exit

exit:
  %result = zext i1 %equal to i32
  store i32 %result, ptr %out, align 4
  ret void
}

attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }

; CHECK-LABEL: define void @compare_pointer_bytes
; CHECK: alloca [8 x i32]
; CHECK: #dbg_declare(ptr poison
; CHECK: store i32 8
; CHECK: store i32 7
; CHECK: store i32 6
; CHECK: store i32 5
; CHECK: store i32 4
; CHECK: store i32 3
; CHECK: store i32 2
; CHECK: store i32 1
; CHECK: phi i64 [ 0, %entry ]
; CHECK: getelementptr [8 x i32]
; CHECK: load i32
; CHECK: add i64

; ABI-LABEL: define void @compare_pointer_bytes()
; ABI: alloca [8 x i32]
; ABI: store i32 8
; ABI: store i32 1
; ABI: phi i64 [ 0, %entry ]
; ABI: load i32
; ABI: store i32

define i32 @signed_integer_byte(i32 %value, i32 %index) {
entry:
  %storage = alloca i32, align 4
  store i32 %value, ptr %storage, align 4
  %address = getelementptr i8, ptr %storage, i32 %index
  %byte = load i8, ptr %address, align 1
  %result = sext i8 %byte to i32
  ret i32 %result
}

; CHECK-LABEL: define i32 @signed_integer_byte
; CHECK: alloca [4 x i32]
; CHECK: and i32 %value, 255
; CHECK: lshr i32 %value, 8
; CHECK: lshr i32 %value, 16
; CHECK: lshr i32 %value, 24
; CHECK: sext i32 %index to i64
; CHECK: getelementptr [4 x i32]
; CHECK: load i32
; CHECK: shl i32 {{.*}}, 24
; CHECK: ashr i32 {{.*}}, 24

define i32 @wrapper_byte(i64 %bits, i32 %index) {
entry:
  %storage = alloca %Wrapper, align 8
  store i64 %bits, ptr %storage, align 8
  %address = getelementptr i8, ptr %storage, i32 %index
  %byte = load i8, ptr %address, align 1
  %result = zext i8 %byte to i32
  ret i32 %result
}

; CHECK-LABEL: define i32 @wrapper_byte
; CHECK: alloca [8 x i32]
; CHECK: lshr i64 %bits, 56
; CHECK: getelementptr [8 x i32]
; CHECK: load i32
; CHECK: and i32 {{.*}}, 255

define i32 @float_byte(float %value, i32 %index) {
entry:
  %storage = alloca float, align 4
  store float %value, ptr %storage, align 4
  %address = getelementptr i8, ptr %storage, i32 %index
  %byte = load i8, ptr %address, align 1
  %result = zext i8 %byte to i32
  ret i32 %result
}

; CHECK-LABEL: define i32 @float_byte
; CHECK: alloca [4 x i32]
; CHECK: bitcast float %value to i32
; CHECK: lshr i32 {{.*}}, 24
; CHECK: getelementptr [4 x i32]
; CHECK: load i32
; CHECK: and i32 {{.*}}, 255

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!8}
!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus, file: !1, producer: "test", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "private-byte.cpp", directory: "/")
!2 = !DISubroutineType(types: !{})
!3 = distinct !DISubprogram(name: "compare_pointer_bytes", scope: !1, file: !1, line: 1, type: !2, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!4 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !5, size: 64)
!5 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!6 = !DILocalVariable(name: "storage", scope: !3, file: !1, line: 1, type: !4)
!7 = !DILocation(line: 1, column: 1, scope: !3)
!8 = !{i32 2, !"Debug Info Version", i32 3}

;--- native.ll
target datalayout = "e-p:32:32"
target triple = "dxil-pc-shadermodel6.0-compute"

define i32 @native_pointer_byte() {
  %storage = alloca ptr, align 4
  store ptr inttoptr (i64 72623859790382856 to ptr), ptr %storage, align 4
  %address = getelementptr i8, ptr %storage, i32 3
  %byte = load i8, ptr %address, align 1
  %result = zext i8 %byte to i32
  ret i32 %result
}

; NATIVE-LABEL: define i32 @native_pointer_byte
; NATIVE: alloca [4 x i32]
; NATIVE: store i32 8
; NATIVE: store i32 7
; NATIVE: store i32 6
; NATIVE: store i32 5
; NATIVE-NOT: store i32 4
; NATIVE: load i32

;--- unsupported.ll
target datalayout = "e-p:64:64"
target triple = "dxil-pc-shadermodel6.0-compute"

define i8 @nonconstant_pointer_byte(ptr %input) {
entry:
  %storage = alloca ptr, align 8
  store ptr %input, ptr %storage, align 8
  br label %loop
loop:
  %address = phi ptr [ %storage, %entry ], [ %next, %loop ]
  %byte = load i8, ptr %address, align 1
  %next = getelementptr i8, ptr %address, i32 1
  %again = icmp eq i8 %byte, 0
  br i1 %again, label %loop, label %exit
exit:
  ret i8 %byte
}

; ERROR: DXIL legalization cannot determine the i8 load's storage type; nonconstant private pointer representations are not supported
