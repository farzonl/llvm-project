// RUN: %clang_cc1 -fsycl-is-device -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -std=c++17 -O1 -emit-llvm %s -o - | FileCheck %s --implicit-check-not=dx.workgroup.specializable
// RUN: not %clang_cc1 -fsycl-is-device -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -std=c++17 -DCONFLICT -emit-llvm %s -o %t.ll 2>&1 | FileCheck %s --check-prefix=CONFLICT

template <class Name, class... Args>
void sycl_kernel_launch(const char *, Args...) {}

template <class Name, class F>
[[clang::sycl_kernel_entry_point(Name)]]
void submit(const F &f) { f(); }

void kernel(unsigned *out) {
  submit<class Geometry>([=]() __attribute__((reqd_work_group_size(4, 2, 2))) {
    *out = __builtin_dx_num_groups(2) +
           static_cast<unsigned>(__builtin_dx_global_offset(1) >> 32);
  });
}

struct Overloaded {
  unsigned *Out;
  void operator()() const __attribute__((reqd_work_group_size(4, 2, 2))) {
    *Out = __builtin_dx_num_groups(0);
  }
  void operator()(int) const __attribute__((reqd_work_group_size(8, 1, 1))) {
    *Out = 0;
  }
};

void overloaded(unsigned *out) {
  submit<class OverloadedGeometry>(Overloaded{out});
}

#ifdef CONFLICT
template <class Name, class F>
[[clang::sycl_kernel_entry_point(Name)]]
void submitConflict(const F &f) {
  f();
  f(0);
}

void conflict(unsigned *out) {
  submitConflict<class ConflictingGeometry>(Overloaded{out});
}
#endif

// CHECK: call i32 @llvm.dx.num.groups(i32 2)
// CHECK: call i64 @llvm.dx.global.offset(i32 1)
// CHECK: define void @{{.*}}(ptr {{.*}}byval(%struct.Overloaded) align 8 {{.*}})
// CHECK: call i32 @llvm.dx.num.groups(i32 0)
// CHECK: "hlsl.numthreads"="4,2,2"
// CHECK-NOT: "hlsl.numthreads"="8,1,1"
// CONFLICT: DirectX kernel calls have conflicting required workgroup sizes
