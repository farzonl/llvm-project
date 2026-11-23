//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// RUN: %clangxx %s -o %t.out
// RUN: %t.out

#include <sycl/sycl.hpp>

#include <cassert>
#include <cmath>
#include <type_traits>

static_assert(sycl::plus<int>{}(13, 6) == 19);
static_assert(sycl::multiplies<>{}(13, 6) == 78);
static_assert(sycl::bit_and<int>{}(13, 6) == 4);
static_assert(sycl::bit_or<>{}(13, 6) == 15);
static_assert(sycl::bit_xor<int>{}(13, 6) == 11);
static_assert(sycl::logical_and<int>{}(13, 0) == 0);
static_assert(sycl::logical_or<>{}(0, 6));
static_assert(sycl::minimum<int>{}(13, 6) == 6);
static_assert(sycl::maximum<>{}(13, 6) == 13);

static_assert(std::is_same_v<decltype(sycl::plus<short>{}(1, 2)), short>);
static_assert(std::is_same_v<decltype(sycl::plus<>{}(short{1}, short{2})), int>);
static_assert(std::is_same_v<decltype(sycl::logical_and<int>{}(1, 2)), int>);
static_assert(std::is_same_v<decltype(sycl::logical_and<>{}(1, 2)), bool>);
static_assert(std::is_same_v<decltype(sycl::minimum<>{}(1, 2.5f)), float>);
static_assert(std::is_same_v<decltype(sycl::maximum<>{}(1.5f, 2.5)), double>);
static_assert(std::is_same_v<
              decltype(sycl::minimum<>{}(std::declval<int &>(),
                                        std::declval<const int &>())),
              int>);

struct BooleanProbe {
  bool Value;
  int *Conversions;
  explicit operator bool() const {
    ++*Conversions;
    return Value;
  }
};

int main() {
  int Conversions = 0;
  assert(!sycl::logical_and<>{}(BooleanProbe{false, &Conversions},
                               BooleanProbe{true, &Conversions}));
  assert(Conversions == 1);
  Conversions = 0;
  assert(sycl::logical_or<>{}(BooleanProbe{true, &Conversions},
                             BooleanProbe{false, &Conversions}));
  assert(Conversions == 1);

  assert(std::signbit(sycl::minimum<float>{}(-0.0f, 0.0f)));
  assert(std::signbit(sycl::maximum<>{}(-0.0f, 0.0f)));
  assert(!std::signbit(sycl::minimum<>{}(0.0f, -0.0f)));
  assert(!std::signbit(sycl::maximum<float>{}(0.0f, -0.0f)));
}
