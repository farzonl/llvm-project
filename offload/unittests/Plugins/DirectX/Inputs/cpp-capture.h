// Generate this fixture with clang -cc1 -x c++ -fsycl-is-device
// -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc
// -std=c++17 -O1 -emit-llvm-bc cpp-capture.h -o cpp-capture.bc,
// opt -dxil-kernel-abi cpp-capture.bc -o cpp-capture.kernel.bc,
// llc -filetype=obj cpp-capture.kernel.bc -o cpp-capture.dxil,
// dxv cpp-capture.dxil -o cpp-capture.dxil, and
// llvm-offload-binary --image=file=cpp-capture.dxil,kind=dxil,
// directx-metadata=cpp-capture.kernel.bc -o cpp-capture.bin.

#ifndef DIRECTX_TEST_CPP_CAPTURE_H
#define DIRECTX_TEST_CPP_CAPTURE_H

inline auto makeVectorAddCapture(const float *a, const float *b, float *c,
                                 unsigned count) {
  return [=] {
#ifdef __DIRECTX__
    unsigned i = __builtin_dx_group_id(0) * 64 + __builtin_dx_thread_id(0);
#else
    unsigned i = 0;
#endif
    if (i < count)
      c[i] = a[i] + b[i];
  };
}

#ifdef __DIRECTX__
template <class Name, class... Args>
void sycl_kernel_launch(const char *, Args...) {}

template <class Name, class F>
[[clang::sycl_kernel_entry_point(Name)]]
__attribute__((reqd_work_group_size(64, 1, 1))) void submitCapture(const F &f) {
  f();
}

void compileVectorAddCapture(const float *a, const float *b, float *c,
                             unsigned count) {
  submitCapture<class CapturedVectorAdd>(makeVectorAddCapture(a, b, c, count));
}
#endif

#endif
