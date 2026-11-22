// RUN: %clang_cc1 -fsycl-is-device -triple dxil-pc-shadermodel6.0-compute -aux-triple x86_64-pc-windows-msvc -std=c++17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -fsycl-is-host -triple x86_64-pc-windows-msvc -std=c++17 -fsyntax-only -verify %s

void ordinary() __attribute__((reqd_work_group_size(4, 1, 1))); // expected-error {{can only be applied to an OpenCL kernel function}}

struct Functor {
  void operator()() const __attribute__((reqd_work_group_size(4, 2, 1)));
  void ordinary() __attribute__((reqd_work_group_size(4, 1, 1))); // expected-error {{can only be applied to an OpenCL kernel function}}
};
