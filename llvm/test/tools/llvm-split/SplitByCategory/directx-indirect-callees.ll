; RUN: llvm-split -split-by-category=kernel -S %s -o %t
; RUN: FileCheck %s --input-file=%t_0.ll

; CHECK-NOT: define {{.*}} @directx_kernel
; CHECK: define {{.*}} @helper
; CHECK: call void %callback(ptr null)
; CHECK: define spir_kernel void @entry
; CHECK-NOT: define {{.*}} @directx_kernel

define void @helper(ptr %callback) {
  call void %callback(ptr null)
  ret void
}

define void @directx_kernel(ptr %capture) #0 {
  store i32 1, ptr %capture
  ret void
}

define spir_kernel void @entry(ptr %callback) {
  call void @helper(ptr %callback)
  ret void
}

attributes #0 = { "dx.kernel" }
