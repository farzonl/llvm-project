// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -fsycl-is-device -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s

// CHECK: target datalayout = "{{.*}}p:64:64"
// CHECK: @Pointer = {{.*}}global ptr null
// CHECK: @Record = {{.*}}global %struct.Capture zeroinitializer

struct Capture {
  void *Pointer;
  unsigned Count;
};
extern "C" {
__attribute__((used)) void *Pointer = nullptr;
__attribute__((used)) Capture Record{};
}

[[clang::sycl_external]] void **getPointer() { return &Pointer; }
[[clang::sycl_external]] Capture *getRecord() { return &Record; }
