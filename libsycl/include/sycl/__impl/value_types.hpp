//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_VALUE_TYPES_HPP
#define _LIBSYCL___IMPL_VALUE_TYPES_HPP

#include <sycl/__impl/half.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

using byte __SYCL2020_DEPRECATED("Use std::byte instead") = std::uint8_t;

template <typename T, int N> class vec;
template <typename T, std::size_t N> class marray;

namespace detail {
template <typename T> struct ValueArraySize {
  static constexpr std::size_t value = 1;
};
template <typename T, int N> struct ValueArraySize<vec<T, N>> {
  static constexpr std::size_t value = N;
};
template <typename T, std::size_t N> struct ValueArraySize<marray<T, N>> {
  static constexpr std::size_t value = N;
};
template <typename T> struct IsValueArray : std::false_type {};
template <typename T, int N>
struct IsValueArray<vec<T, N>> : std::true_type {};
template <typename T, std::size_t N>
struct IsValueArray<marray<T, N>> : std::true_type {};
template <typename T>
using VectorMaskElement = std::conditional_t<
    sizeof(T) == 1, std::int8_t,
    std::conditional_t<sizeof(T) == 2, std::int16_t,
                       std::conditional_t<sizeof(T) == 4, std::int32_t,
                                          std::int64_t>>>;
} // namespace detail

template <typename T, std::size_t N> class marray {
  static_assert(N > 0);
  std::array<T, N> Elements{};

  template <typename A> constexpr void append(std::size_t &Index, const A &Arg) {
    if constexpr (detail::IsValueArray<A>::value)
      for (std::size_t I = 0; I != A::size(); ++I)
        Elements[Index++] = Arg[I];
    else
      Elements[Index++] = Arg;
  }

public:
  using value_type = T;
  using reference = T &;
  using const_reference = const T &;
  using iterator = T *;
  using const_iterator = const T *;

  constexpr marray() = default;
  explicit constexpr marray(const T &Value) {
    for (auto &Element : Elements)
      Element = Value;
  }
  template <typename... A,
            std::enable_if_t<(detail::ValueArraySize<A>::value + ...) == N,
                             int> = 0>
  constexpr marray(const A &...Args) {
    std::size_t Index = 0;
    (append(Index, Args), ...);
  }
  static constexpr std::size_t size() { return N; }
  constexpr T &operator[](std::size_t I) { return Elements[I]; }
  constexpr const T &operator[](std::size_t I) const { return Elements[I]; }
  constexpr T *begin() { return Elements.data(); }
  constexpr const T *begin() const { return Elements.data(); }
  constexpr const T *cbegin() const { return begin(); }
  constexpr T *end() { return begin() + N; }
  constexpr const T *end() const { return begin() + N; }
  constexpr const T *cend() const { return end(); }
};

template <typename T, int N>
class alignas(sizeof(T) * (N == 3 ? 4 : N)) vec {
  static_assert(N == 1 || N == 2 || N == 3 || N == 4 || N == 8 || N == 16);
  std::array<T, N == 3 ? 4 : N> Elements{};

  template <typename A> constexpr void append(int &Index, const A &Arg) {
    if constexpr (detail::IsValueArray<A>::value)
      for (std::size_t I = 0; I != A::size(); ++I)
        Elements[Index++] = Arg[I];
    else
      Elements[Index++] = Arg;
  }

public:
  using element_type = T;
  using value_type = T;
  constexpr vec() = default;
  explicit constexpr vec(const T &Value) {
    for (int I = 0; I != N; ++I)
      Elements[I] = Value;
  }
  template <typename... A,
            std::enable_if_t<(detail::ValueArraySize<A>::value + ...) == N,
                             int> = 0>
  constexpr vec(const A &...Args) {
    int Index = 0;
    (append(Index, Args), ...);
  }
  static constexpr std::size_t size() { return N; }
  static constexpr std::size_t byte_size() { return sizeof(vec); }
  __SYCL2020_DEPRECATED("Use size instead")
  static constexpr std::size_t get_count() { return size(); }
  __SYCL2020_DEPRECATED("Use byte_size instead")
  static constexpr std::size_t get_size() { return byte_size(); }
  constexpr T &operator[](int I) { return Elements[I]; }
  constexpr const T &operator[](int I) const { return Elements[I]; }
  template <typename R> R as() const {
    static_assert(sizeof(R) == sizeof(vec));
    return sycl::bit_cast<R>(*this);
  }
  template <typename R> constexpr vec<R, N> convert() const {
    vec<R, N> Result;
    for (int I = 0; I != N; ++I)
      Result[I] = static_cast<R>(Elements[I]);
    return Result;
  }

#define _LIBSYCL_VEC_COMPONENT(Name, Index)                                     \
  template <int M = N, std::enable_if_t<(M > Index), int> = 0>                  \
  constexpr T &Name() { return Elements[Index]; }                             \
  template <int M = N, std::enable_if_t<(M > Index), int> = 0>                  \
  constexpr const T &Name() const { return Elements[Index]; }
  _LIBSYCL_VEC_COMPONENT(x, 0)
  _LIBSYCL_VEC_COMPONENT(y, 1)
  _LIBSYCL_VEC_COMPONENT(z, 2)
  _LIBSYCL_VEC_COMPONENT(w, 3)
  _LIBSYCL_VEC_COMPONENT(s0, 0)
  _LIBSYCL_VEC_COMPONENT(s1, 1)
  _LIBSYCL_VEC_COMPONENT(s2, 2)
  _LIBSYCL_VEC_COMPONENT(s3, 3)
  _LIBSYCL_VEC_COMPONENT(s4, 4)
  _LIBSYCL_VEC_COMPONENT(s5, 5)
  _LIBSYCL_VEC_COMPONENT(s6, 6)
  _LIBSYCL_VEC_COMPONENT(s7, 7)
  _LIBSYCL_VEC_COMPONENT(s8, 8)
  _LIBSYCL_VEC_COMPONENT(s9, 9)
  _LIBSYCL_VEC_COMPONENT(sA, 10)
  _LIBSYCL_VEC_COMPONENT(sB, 11)
  _LIBSYCL_VEC_COMPONENT(sC, 12)
  _LIBSYCL_VEC_COMPONENT(sD, 13)
  _LIBSYCL_VEC_COMPONENT(sE, 14)
  _LIBSYCL_VEC_COMPONENT(sF, 15)
#undef _LIBSYCL_VEC_COMPONENT
};

#define _LIBSYCL_VALUE_BINARY(Class, SizeType, Op)                              \
  template <typename T, SizeType N>                                            \
  constexpr Class<T, N> operator Op(const Class<T, N> &X,                       \
                                    const Class<T, N> &Y) {                    \
    Class<T, N> Result;                                                        \
    for (std::size_t I = 0; I != N; ++I)                                       \
      Result[I] = X[I] Op Y[I];                                                \
    return Result;                                                            \
  }                                                                           \
  template <typename T, SizeType N>                                            \
  constexpr Class<T, N> operator Op(const Class<T, N> &X, const T &Y) {         \
    return X Op Class<T, N>(Y);                                                \
  }                                                                           \
  template <typename T, SizeType N>                                            \
  constexpr Class<T, N> operator Op(const T &X, const Class<T, N> &Y) {         \
    return Class<T, N>(X) Op Y;                                                \
  }

#define _LIBSYCL_VALUE_ARITHMETIC(Class, SizeType)                               \
  _LIBSYCL_VALUE_BINARY(Class, SizeType, +)                                     \
  _LIBSYCL_VALUE_BINARY(Class, SizeType, -)                                     \
  _LIBSYCL_VALUE_BINARY(Class, SizeType, *)                                     \
  _LIBSYCL_VALUE_BINARY(Class, SizeType, /)
_LIBSYCL_VALUE_ARITHMETIC(vec, int)
_LIBSYCL_VALUE_ARITHMETIC(marray, std::size_t)
#undef _LIBSYCL_VALUE_ARITHMETIC
#undef _LIBSYCL_VALUE_BINARY

#define _LIBSYCL_VALUE_COMPARE(Class, SizeType, ResultType, TrueValue, Op)       \
  template <typename T, SizeType N>                                            \
  constexpr Class<ResultType, N> operator Op(const Class<T, N> &X,             \
                                             const Class<T, N> &Y) {           \
    Class<ResultType, N> Result;                                               \
    for (std::size_t I = 0; I != N; ++I)                                       \
      Result[I] = (X[I] Op Y[I]) ? TrueValue : 0;                               \
    return Result;                                                            \
  }
#define _LIBSYCL_VALUE_COMPARISONS(Op)                                          \
  _LIBSYCL_VALUE_COMPARE(vec, int, detail::VectorMaskElement<T>, -1, Op)        \
  _LIBSYCL_VALUE_COMPARE(marray, std::size_t, bool, true, Op)
_LIBSYCL_VALUE_COMPARISONS(==)
_LIBSYCL_VALUE_COMPARISONS(!=)
_LIBSYCL_VALUE_COMPARISONS(<)
_LIBSYCL_VALUE_COMPARISONS(>)
_LIBSYCL_VALUE_COMPARISONS(<=)
_LIBSYCL_VALUE_COMPARISONS(>=)
_LIBSYCL_VALUE_COMPARISONS(&&)
_LIBSYCL_VALUE_COMPARISONS(||)
#undef _LIBSYCL_VALUE_COMPARISONS
#undef _LIBSYCL_VALUE_COMPARE

#define _LIBSYCL_VALUE_ALIASES(Name, Type, N)                                   \
  using Name##N = vec<Type, N>;                                                \
  using m##Name##N = marray<Type, N>;
#define _LIBSYCL_VALUE_ALIAS_SIZES(Name, Type)                                  \
  _LIBSYCL_VALUE_ALIASES(Name, Type, 1)                                         \
  _LIBSYCL_VALUE_ALIASES(Name, Type, 2)                                         \
  _LIBSYCL_VALUE_ALIASES(Name, Type, 3)                                         \
  _LIBSYCL_VALUE_ALIASES(Name, Type, 4)                                         \
  _LIBSYCL_VALUE_ALIASES(Name, Type, 8)                                         \
  _LIBSYCL_VALUE_ALIASES(Name, Type, 16)
_LIBSYCL_VALUE_ALIAS_SIZES(char, std::int8_t)
_LIBSYCL_VALUE_ALIAS_SIZES(uchar, std::uint8_t)
_LIBSYCL_VALUE_ALIAS_SIZES(short, std::int16_t)
_LIBSYCL_VALUE_ALIAS_SIZES(ushort, std::uint16_t)
_LIBSYCL_VALUE_ALIAS_SIZES(int, std::int32_t)
_LIBSYCL_VALUE_ALIAS_SIZES(uint, std::uint32_t)
_LIBSYCL_VALUE_ALIAS_SIZES(long, std::int64_t)
_LIBSYCL_VALUE_ALIAS_SIZES(ulong, std::uint64_t)
_LIBSYCL_VALUE_ALIAS_SIZES(float, float)
_LIBSYCL_VALUE_ALIAS_SIZES(double, double)
_LIBSYCL_VALUE_ALIAS_SIZES(half, half)
#undef _LIBSYCL_VALUE_ALIAS_SIZES
#undef _LIBSYCL_VALUE_ALIASES

_LIBSYCL_END_NAMESPACE_SYCL

#endif // _LIBSYCL___IMPL_VALUE_TYPES_HPP
