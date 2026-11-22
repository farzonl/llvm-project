// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -fsyntax-only -verify %s

unsigned test(int dimension) {
  __builtin_dx_thread_id(3); // expected-error {{outside the valid range}}
  __builtin_dx_group_id(-1); // expected-error {{outside the valid range}}
  __builtin_dx_group_size(dimension); // expected-error {{must be a constant integer}}
  return __builtin_dx_thread_id(2);
}
