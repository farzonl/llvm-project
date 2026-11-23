// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -fsycl-is-device -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s

struct [[clang::trivial_abi]] Resource {
  ~Resource();
};

[[clang::sycl_external]] void cleanup(Resource R) {}

// CHECK-LABEL: define {{.*}} @_Z7cleanup8Resource(
// CHECK: %[[ENTRY:.*]] = call token @llvm.experimental.convergence.entry()
// CHECK: call void @_ZN8ResourceD1Ev{{.*}}"convergencectrl"(token %[[ENTRY]])
