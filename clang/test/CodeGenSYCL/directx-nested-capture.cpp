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

struct ArrayCallable {
  Accessor Outputs[2];
  unsigned long long Values[2][2];
  void operator()() const {
    Outputs[1].Data[0] = static_cast<int>(Values[1][1]);
  }
};
class ArrayCapture;
[[clang::sycl_kernel_entry_point(ArrayCapture)]]
void launch_array(ArrayCallable Function) {
  Function();
}

// CHECK: ![[ARRAY:[0-9]+]] = !{i32 80, i32 8, ![[DATA]], ![[OWNER]], ![[COUNT]], ![[SECOND_DATA:[0-9]+]], ![[SECOND_OWNER:[0-9]+]], ![[SECOND_COUNT:[0-9]+]], ![[V0:[0-9]+]], ![[V1:[0-9]+]], ![[V2:[0-9]+]], ![[V3:[0-9]+]]}
// CHECK: ![[SECOND_DATA]] = !{i32 24, i32 8, i32 1}
// CHECK: ![[SECOND_OWNER]] = !{i32 32, i32 8}
// CHECK: ![[SECOND_COUNT]] = !{i32 40, i32 8}
// CHECK: ![[V0]] = !{i32 48, i32 8}
// CHECK: ![[V1]] = !{i32 56, i32 8}
// CHECK: ![[V2]] = !{i32 64, i32 8}
// CHECK: ![[V3]] = !{i32 72, i32 8}

struct CaptureBase {
  unsigned long long Dimensions[2];
};
struct DerivedCallable : CaptureBase {
  Accessor Output;
  void operator()() const {
    Output.Data[0] = static_cast<int>(Dimensions[1]);
  }
};
class DerivedCapture;
[[clang::sycl_kernel_entry_point(DerivedCapture)]]
void launch_derived(DerivedCallable Function) {
  Function();
}

// CHECK: ![[DERIVED:[0-9]+]] = !{i32 40, i32 8, ![[BASE0:[0-9]+]], ![[OWNER]], ![[DERIVED_DATA:[0-9]+]], ![[DERIVED_OWNER:[0-9]+]], ![[SECOND_OWNER]]}
// CHECK: ![[BASE0]] = !{i32 0, i32 8}
// CHECK: ![[DERIVED_DATA]] = !{i32 16, i32 8, i32 1}
// CHECK: ![[DERIVED_OWNER]] = !{i32 24, i32 8}
