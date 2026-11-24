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
#include <limits>

static_assert(sycl::bit_cast<std::uint16_t>(sycl::half(1.0)) == 0x3c00);
static_assert(sycl::bit_cast<std::uint16_t>(
                  std::numeric_limits<sycl::half>::denorm_min()) == 1);

constexpr int First = 3;
constexpr int Second = 5;
constexpr sycl::vec Deduced(First, Second, 7);
static_assert(
    std::is_same_v<std::remove_cv_t<decltype(Deduced)>, sycl::vec<int, 3>>);
static_assert(Deduced[0] == 3 && Deduced[1] == 5 && Deduced[2] == 7);
constexpr sycl::vec Copied(Deduced);
static_assert(
    std::is_same_v<std::remove_cv_t<decltype(Copied)>, sycl::vec<int, 3>>);
static_assert(Copied[2] == 7);
constexpr sycl::vec Single(First);
static_assert(
    std::is_same_v<std::remove_cv_t<decltype(Single)>, sycl::vec<int, 1>>);
static_assert(Single[0] == 3);

int main() {
  for (unsigned Bits = 0; Bits != 65536; ++Bits) {
    auto Value = sycl::bit_cast<sycl::half>(static_cast<std::uint16_t>(Bits));
    float F = Value;
    if ((Bits & 0x7c00) == 0x7c00 && (Bits & 1023))
      assert(std::isnan(F));
    else
      assert(sycl::bit_cast<std::uint16_t>(sycl::half(F)) == Bits);
  }
  double Midpoint = 1.0 + std::ldexp(1.0, -11);
  assert(sycl::bit_cast<std::uint16_t>(sycl::half(Midpoint)) == 0x3c00);
  assert(sycl::bit_cast<std::uint16_t>(
             sycl::half(std::nextafter(Midpoint, 2.0))) == 0x3c01);
  assert(sycl::bit_cast<std::uint16_t>(
             sycl::half(std::nextafter(Midpoint, 0.0))) == 0x3c00);
  assert(sycl::bit_cast<std::uint16_t>(sycl::half(65519.0)) == 0x7bff);
  assert(std::isinf(static_cast<float>(sycl::half(65520.0))));
  assert(std::signbit(static_cast<float>(sycl::half(-0.0))));
  assert(sycl::half(std::ldexp(1.0, -25)) == 0.0f);
  assert(sycl::bit_cast<std::uint16_t>(
             sycl::half(std::nextafter(std::ldexp(1.0, -25), 1.0))) == 1);

  sycl::float4 A(sycl::float2(1.0f, 2.0f), sycl::float2(3.0f, 4.0f));
  auto Sum = A + sycl::float4(2.0f);
  for (unsigned I = 0; I != 4; ++I)
    assert(Sum[I] == static_cast<float>(I + 3));
  auto Mask = A > sycl::float4(2.0f);
  assert(Mask[0] == 0 && Mask[1] == 0 && Mask[2] == -1 && Mask[3] == -1);
  assert(A.x() == 1.0f && A.w() == 4.0f);
  sycl::float3 Padded(1.0f, 2.0f, 3.0f);
  assert(Padded.byte_size() == 4 * sizeof(float));
  auto Raw = Padded.as<sycl::uint4>();
  assert(Raw[2] == sycl::bit_cast<unsigned>(3.0f));

  sycl::marray<int, 5> B(sycl::marray<int, 2>(1, 2), 3,
                        sycl::marray<int, 2>(4, 5));
  auto Product = B * sycl::marray<int, 5>(2);
  auto ArrayMask = B < sycl::marray<int, 5>(4);
  for (unsigned I = 0; I != 5; ++I) {
    assert(Product[I] == static_cast<int>((I + 1) * 2));
    assert(ArrayMask[I] == (I < 3));
  }
}
