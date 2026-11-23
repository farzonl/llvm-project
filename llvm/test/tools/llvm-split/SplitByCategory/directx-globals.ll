; RUN: llvm-split -split-by-category=kernel -S %s -o %t
; RUN: opt -S -passes=strip-dead-prototypes %t_1.ll -o %t.none.ll
; RUN: opt -S -passes=strip-dead-prototypes %t_0.ll -o %t.needed.ll
; RUN: FileCheck %s --check-prefix=NONE --input-file=%t.none.ll
; RUN: FileCheck %s --check-prefix=NEEDED --input-file=%t_0.ll
; RUN: opt -S -dxil-kernel-abi %t.none.ll | FileCheck %s --check-prefix=LOWER
; RUN: not opt -S -dxil-kernel-abi %t.needed.ll 2>&1 | FileCheck %s --check-prefix=DYNAMIC
; RUN: opt -S -mtriple=spir64-unknown-unknown %s -o %t.spir.ll
; RUN: llvm-split -split-by-category=kernel -S %t.spir.ll -o %t.spir
; RUN: FileCheck %s --check-prefix=SPIR --input-file=%t.spir_0.ll

target datalayout = "e-p:64:64"
target triple = "dxil-pc-shadermodel6.0-compute"

; NONE-NOT: @llvm.global_ctors
; NONE-NOT: @llvm.global_dtors
; NONE-NOT: define void @host_ctor
; NONE-NOT: define i64 @host_helper
; NONE: define spir_kernel void @entry_none
; NONE-NOT: define void @host_ctor
; NONE-NOT: define i64 @host_helper
; LOWER: define void @entry_none()
; DYNAMIC: dynamic device initialization and destruction are not supported

; NEEDED: @device_a = global i32 0
; NEEDED: @device_b = global i32 0
; NEEDED: @llvm.global_ctors = appending global [3 x
; NEEDED-SAME: i32 65535, ptr @ctor_a, ptr @device_a
; NEEDED-SAME: i32 12, ptr @ctor_b, ptr null
; NEEDED-SAME: i32 20, ptr @opaque_init, ptr null
; NEEDED: @llvm.global_dtors = appending global [1 x
; NEEDED-SAME: i32 65535, ptr @dtor_a, ptr @device_a
; NEEDED: define void @ctor_a
; NEEDED: define void @ctor_b
; NEEDED-NOT: define void @host_ctor
; NEEDED-NOT: define i64 @host_helper
; NEEDED: define void @dtor_a

; SPIR: @host_state = global ptr null
; SPIR: @llvm.global_ctors = appending global [4 x
; SPIR: define void @host_ctor
; SPIR: define i64 @host_helper

@device_a = global i32 0
@device_b = global i32 0
@host_state = global ptr null
@llvm.global_ctors = appending global [4 x { i32, ptr, ptr }] [
  { i32, ptr, ptr } { i32 1, ptr @host_ctor, ptr null },
  { i32, ptr, ptr } { i32 65535, ptr @ctor_a, ptr @device_a },
  { i32, ptr, ptr } { i32 12, ptr @ctor_b, ptr null },
  { i32, ptr, ptr } { i32 20, ptr @opaque_init, ptr null }]
@llvm.global_dtors = appending global [1 x { i32, ptr, ptr }] [
  { i32, ptr, ptr } { i32 65535, ptr @dtor_a, ptr @device_a }]

define void @ctor_a() {
  %value = load i32, ptr @device_b
  store i32 %value, ptr @device_a
  ret void
}

define void @ctor_b() {
  store i32 23, ptr @device_b
  ret void
}

define void @host_ctor() {
  %value = call i64 @host_helper(ptr @host_state)
  ret void
}

define i64 @host_helper(ptr %address) {
  %value = ptrtoaddr ptr %address to i64
  ret i64 %value
}

declare void @opaque_init()

define void @dtor_a() {
  store i32 0, ptr @device_a
  ret void
}

define spir_kernel void @entry_none(ptr %output) #0 {
  store i32 1, ptr %output
  ret void
}

define spir_kernel void @entry_needed(ptr %output) #0 {
  %value = load i32, ptr @device_a
  store i32 %value, ptr %output
  ret void
}

attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }
