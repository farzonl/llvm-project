// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -fsycl-is-device -disable-llvm-passes -emit-llvm -o - %s | FileCheck %s

using LocalInt = int [[clang::sycl_local]];
struct LocalAccessor {
  [[clang::annotate("sycl.local.pointer")]] LocalInt *Data;
  unsigned long long Count;
};
struct Callable {
  LocalAccessor Local;
  int *Output;
  void operator()() const {
    Local.Data[0] = 42;
    Output[0] = Local.Data[0];
  }
};
template <typename KernelName, typename... Args>
void sycl_kernel_launch(const char *, Args...) {}
class LocalCapture;
[[clang::sycl_kernel_entry_point(LocalCapture)]]
void launch(Callable Function) {
  Function();
}

// CHECK: define {{.*}} @_ZTS12LocalCapture(ptr {{.*}}byval(%struct.Callable)
// CHECK: !dx.capture.layout ![[LAYOUT:[0-9]+]]
// CHECK: attributes #{{[0-9]+}} = {{.*}}"dx.local.specializable"
// CHECK: ![[LAYOUT]] = !{i32 24, i32 8, ![[LOCAL:[0-9]+]], ![[COUNT:[0-9]+]], ![[OUTPUT:[0-9]+]]}
// CHECK: ![[LOCAL]] = !{i32 0, i32 8, i32 2, i32 4}
// CHECK: ![[COUNT]] = !{i32 8, i32 8}
// CHECK: ![[OUTPUT]] = !{i32 16, i32 8}
