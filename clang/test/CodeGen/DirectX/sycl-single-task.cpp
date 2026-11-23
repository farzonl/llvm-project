// RUN: %clang_cc1 -fsycl-is-device -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -std=c++17 -O1 -emit-llvm %s -o - | FileCheck %s

static_assert(__is_same(decltype(sizeof(0)), unsigned long long));
static_assert(__is_same(decltype((int *)0 - (int *)0), long long));

template <class Name, class... Args>
void sycl_kernel_launch(const char *, Args...) {}

template <class Name, class F>
[[clang::sycl_kernel_entry_point(Name)]]
void submit(const F &f) {
  f();
}

void single_task(float *out) {
  submit<class SingleTask>([=] { *out = 1.0f; });
}

struct Abstract {
  virtual void f() = 0;
  virtual ~Abstract();
};
Abstract::~Abstract() {}

// CHECK: declare void @__cxa_pure_virtual() {{.*}}#[[PURE:[0-9]+]]
// CHECK: define void @{{.*}}(ptr {{.*}}byval(%class.anon) align 8 {{.*}})
// CHECK: attributes #[[PURE]] = { {{.*}}"dontcall-error"="DirectX kernels do not support pure or deleted virtual calls"
// CHECK: "dx.workgroup.specializable"{{.*}}"hlsl.numthreads"="1,1,1"
