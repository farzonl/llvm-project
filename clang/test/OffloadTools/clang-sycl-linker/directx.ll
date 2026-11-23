; REQUIRES: directx-registered-target
; RUN: split-file %s %t
; RUN: llvm-as %t/kernels.ll -o %t/kernels.bc
; RUN: llvm-as %t/empty.ll -o %t/empty.bc
; RUN: clang-sycl-linker --dry-run -v --triple=dxil-pc-shadermodel6.0-compute --dxil-validator=custom-dxv %t/kernels.bc %t/empty.bc -o %t/output.bin 2>&1 | FileCheck %s --check-prefix=LINK
; RUN: not clang-sycl-linker --dry-run --module-split-mode=link_unit %t/kernels.bc -o %t/output.bin 2>&1 | FileCheck %s --check-prefix=SPLIT
; RUN: not clang-sycl-linker --dry-run %t/empty.bc -o %t/output.bin 2>&1 | FileCheck %s --check-prefix=EMPTY
; RUN: not clang-sycl-linker --dxil-validator=nonexistent-directx-validator %t/kernels.bc -o %t/output.bin 2>&1 | FileCheck %s --check-prefix=MISSING
; RUN: not clang-sycl-linker --dxil-validator=llvm-as %t/kernels.bc -o %t/output.bin 2>&1 | FileCheck %s --check-prefix=FAILURE

; LINK: sycl-module-split: input: {{.*}}, mode: kernel
; LINK-NEXT: {{.*}} [second ]
; LINK-NEXT: {{.*}} [first ]
; LINK: "opt" -dxil-kernel-abi
; LINK: "llc" -filetype=obj
; LINK: "custom-dxv" -o=
; LINK: "llvm-offload-binary" -o {{.*}} --image=file={{.*}},kind=sycl,triple=dxilv1.0-pc-shadermodel6.0-compute,directx-metadata=
; LINK: "opt" -dxil-kernel-abi
; LINK: "llc" -filetype=obj
; LINK: "custom-dxv" -o=
; LINK: "llvm-offload-binary"
; SPLIT: DirectX SYCL requires --module-split-mode=kernel
; EMPTY: DirectX SYCL device link contains no kernels
; MISSING: unable to find 'nonexistent-directx-validator' in path
; FAILURE: 'llvm-as{{(\.exe)?}}' failed

;--- kernels.ll
target datalayout = "e-p:32:32-i64:64"
target triple = "dxilv1.0-pc-shadermodel6.0-compute"

define void @first(ptr %output) #0 {
  store i32 7, ptr %output, align 4
  ret void
}

define void @second(ptr %output) #0 {
  store i32 9, ptr %output, align 4
  ret void
}

attributes #0 = { "dx.kernel" "hlsl.numthreads"="1,1,1" }

;--- empty.ll
target datalayout = "e-p:32:32-i64:64"
target triple = "dxilv1.0-pc-shadermodel6.0-compute"

define i32 @helper() {
  ret i32 0
}
