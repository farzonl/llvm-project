// RUN: %clang_cc1 -fsycl-is-device -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -std=c++17 -O1 -emit-llvm %s -o - | FileCheck %s

template <class Name, class... Args>
void sycl_kernel_launch(const char *, Args...) {}

template <class Name, class F>
[[clang::sycl_kernel_entry_point(Name)]]
__attribute__((reqd_work_group_size(64, 1, 1)))
void submit(const F &f) {
  f();
}

void vector_add(const float *a, const float *b, float *c, unsigned count) {
  submit<class CapturedVectorAdd>([=] {
    unsigned i = __builtin_dx_group_id(0) * 64 + __builtin_dx_thread_id(0);
    if (i < count)
      c[i] = a[i] + b[i];
  });
}

// CHECK: define void @{{.*}}(ptr {{.*}}byval(%class.anon) align 8 {{.*}}) {{.*}}!dx.capture.layout ![[LAYOUT:[0-9]+]]
// CHECK: call i32 @llvm.dx.group.id
// CHECK: call i32 @llvm.dx.thread.id.in.group
// CHECK: "dx.kernel"
// CHECK: "hlsl.numthreads"="64,1,1"
// CHECK: ![[LAYOUT]] = !{i32 32, i32 8, ![[COUNT:[0-9]+]], ![[C:[0-9]+]], ![[A:[0-9]+]], ![[B:[0-9]+]]}
// CHECK: ![[COUNT]] = !{i32 0, i32 4}
// CHECK: ![[C]] = !{i32 8, i32 8}
// CHECK: ![[A]] = !{i32 16, i32 8}
// CHECK: ![[B]] = !{i32 24, i32 8}
