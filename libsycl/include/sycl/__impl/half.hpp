//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_HALF_HPP
#define _LIBSYCL___IMPL_HALF_HPP

#include <sycl/__impl/bit_cast.hpp>
#include <cstdint>
#include <limits>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

class half {
  std::uint16_t Bits = 0;

  static constexpr std::uint16_t encode(double Value) {
    std::uint64_t Raw = sycl::bit_cast<std::uint64_t>(Value);
    unsigned Sign = (Raw >> 48) & 0x8000;
    unsigned Exponent = (Raw >> 52) & 0x7ff;
    std::uint64_t Mantissa = Raw & 0xfffffffffffffULL;
    if (Exponent == 0x7ff)
      return Sign | 0x7c00 |
             (Mantissa ? ((Mantissa >> 42) | 0x200) : 0);
    int E = static_cast<int>(Exponent) - 1023;
    if (E < -25)
      return Sign;
    if (E > 15)
      return Sign | 0x7c00;
    std::uint64_t Significand = Mantissa | (1ULL << 52);
    unsigned Shift = E < -14 ? 28 - E : 42;
    std::uint64_t Rounded = Significand >> Shift;
    std::uint64_t Remainder = Significand & ((1ULL << Shift) - 1);
    std::uint64_t Midpoint = 1ULL << (Shift - 1);
    // Round once from the original significand, including double inputs.
    Rounded += Remainder > Midpoint ||
               (Remainder == Midpoint && (Rounded & 1));
    return Sign | (E < -14 ? Rounded : ((E + 15) << 10) + Rounded - 1024);
  }

  static constexpr half fromBits(std::uint16_t Value) {
    half Result;
    Result.Bits = Value;
    return Result;
  }
  friend class std::numeric_limits<half>;

public:
  constexpr half() = default;
  constexpr half(double Value) : Bits(encode(Value)) {}

  constexpr operator float() const {
    unsigned Sign = (Bits & 0x8000U) << 16;
    unsigned Exponent = (Bits >> 10) & 31;
    unsigned Mantissa = Bits & 1023;
    unsigned Raw = Sign;
    if (Exponent == 31) {
      Raw |= 0x7f800000U | (Mantissa << 13);
    } else if (Exponent) {
      Raw |= ((Exponent + 112) << 23) | (Mantissa << 13);
    } else if (Mantissa) {
      unsigned E = 113;
      while ((Mantissa & 1024) == 0) {
        Mantissa <<= 1;
        --E;
      }
      Raw |= (E << 23) | ((Mantissa & 1023) << 13);
    }
    return sycl::bit_cast<float>(Raw);
  }

  constexpr half &operator+=(half Other) {
    return *this = half(static_cast<float>(*this) + static_cast<float>(Other));
  }
  constexpr half &operator-=(half Other) {
    return *this = half(static_cast<float>(*this) - static_cast<float>(Other));
  }
  constexpr half &operator*=(half Other) {
    return *this = half(static_cast<float>(*this) * static_cast<float>(Other));
  }
  constexpr half &operator/=(half Other) {
    return *this = half(static_cast<float>(*this) / static_cast<float>(Other));
  }
  constexpr half &operator++() { return *this += half(1); }
  constexpr half operator++(int) {
    half Previous = *this;
    ++*this;
    return Previous;
  }
  constexpr half &operator--() { return *this -= half(1); }
  constexpr half operator--(int) {
    half Previous = *this;
    --*this;
    return Previous;
  }
};

_LIBSYCL_END_NAMESPACE_SYCL

namespace std {
template <> class numeric_limits<sycl::half> {
public:
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = false;
  static constexpr bool is_exact = false;
  static constexpr int radix = 2;
  static constexpr int digits = 11;
  static constexpr int digits10 = 3;
  static constexpr int max_digits10 = 5;
  static constexpr int min_exponent = -13;
  static constexpr int min_exponent10 = -4;
  static constexpr int max_exponent = 16;
  static constexpr int max_exponent10 = 4;
  static constexpr bool has_infinity = true;
  static constexpr bool has_quiet_NaN = true;
  static constexpr bool has_signaling_NaN = true;
  static constexpr float_denorm_style has_denorm = denorm_present;
  static constexpr bool has_denorm_loss = false;
  static constexpr bool is_iec559 = true;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr bool traps = false;
  static constexpr bool tinyness_before = false;
  static constexpr float_round_style round_style = round_to_nearest;
  static constexpr sycl::half min() noexcept {
    return sycl::half::fromBits(0x0400);
  }
  static constexpr sycl::half max() noexcept {
    return sycl::half::fromBits(0x7bff);
  }
  static constexpr sycl::half lowest() noexcept {
    return sycl::half::fromBits(0xfbff);
  }
  static constexpr sycl::half epsilon() noexcept {
    return sycl::half::fromBits(0x1400);
  }
  static constexpr sycl::half round_error() noexcept {
    return sycl::half::fromBits(0x3800);
  }
  static constexpr sycl::half infinity() noexcept {
    return sycl::half::fromBits(0x7c00);
  }
  static constexpr sycl::half quiet_NaN() noexcept {
    return sycl::half::fromBits(0x7e00);
  }
  static constexpr sycl::half signaling_NaN() noexcept {
    return sycl::half::fromBits(0x7d00);
  }
  static constexpr sycl::half denorm_min() noexcept {
    return sycl::half::fromBits(0x0001);
  }
};
} // namespace std

#endif // _LIBSYCL___IMPL_HALF_HPP
