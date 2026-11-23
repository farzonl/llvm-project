// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -fsycl-is-device -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s

struct Accessor {
  [[clang::annotate("sycl.accessor.pointer")]] int *Data;
  void *Owner;
  unsigned long long Count;
  Accessor(const Accessor &Other)
      : Data(Other.Data), Owner(Other.Owner), Count(Other.Count) {}
  ~Accessor() {}
};
struct Callable {
  Accessor Output;
  void operator()() const { Output.Data[0] = 42; }
};
template <typename KernelName, typename... Args>
void sycl_kernel_launch(const char *, Args...) {}
class NestedCapture;
[[clang::sycl_kernel_entry_point(NestedCapture)]]
void launch(Callable Function) {
  Function();
}

// CHECK: define {{.*}} @_ZTS13NestedCapture(ptr {{.*}}byval(%struct.Callable)
// CHECK: !dx.capture.layout ![[LAYOUT:[0-9]+]]
// CHECK: ![[LAYOUT]] = !{i32 24, i32 8, ![[DATA:[0-9]+]], ![[OWNER:[0-9]+]], ![[COUNT:[0-9]+]]}
// CHECK: ![[DATA]] = !{i32 0, i32 8, i32 1}
// CHECK: ![[OWNER]] = !{i32 8, i32 8}
// CHECK: ![[COUNT]] = !{i32 16, i32 8}
