// REQUIRES: directx-registered-target, x86-registered-target
// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -emit-llvm-bc %s -o %t.bc
// RUN: llvm-offload-binary -o %t.bin --image=file=%t.bc,kind=sycl,triple=dxilv1.0-pc-shadermodel6.0-compute,arch=generic
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -emit-obj -fembed-offload-object=%t.bin %s -o %t.obj
// RUN: clang-linker-wrapper --dry-run --host-triple=x86_64-pc-windows-msvc --device-linker=dxilv1.0-pc-shadermodel6.0-compute=--dxil-validator=custom-dxv --linker-path=lld-link %t.obj /out:%t.exe 2>&1 | FileCheck %s

// CHECK: clang{{.*}} --target=dxilv1.0-pc-shadermodel6.0-compute
// CHECK-NOT: --no-undefined
// CHECK-SAME: --sycl-link -Xlinker --dxil-validator=custom-dxv
// CHECK: lld-link

void f() {}
