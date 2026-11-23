//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_FUNCTIONAL_HPP
#define _LIBSYCL___IMPL_FUNCTIONAL_HPP

#include <sycl/__impl/detail/config.hpp>

#include <type_traits>
#include <utility>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

template <typename T = void> struct plus {
  constexpr T operator()(const T &X, const T &Y) const { return X + Y; }
};

template <> struct plus<void> {
  template <typename T, typename U>
  constexpr auto operator()(T &&X, U &&Y) const
      -> decltype(std::forward<T>(X) + std::forward<U>(Y)) {
    return std::forward<T>(X) + std::forward<U>(Y);
  }
};

template <typename T = void> struct multiplies {
  constexpr T operator()(const T &X, const T &Y) const { return X * Y; }
};

template <> struct multiplies<void> {
  template <typename T, typename U>
  constexpr auto operator()(T &&X, U &&Y) const
      -> decltype(std::forward<T>(X) * std::forward<U>(Y)) {
    return std::forward<T>(X) * std::forward<U>(Y);
  }
};

template <typename T = void> struct bit_and {
  constexpr T operator()(const T &X, const T &Y) const { return X & Y; }
};

template <> struct bit_and<void> {
  template <typename T, typename U>
  constexpr auto operator()(T &&X, U &&Y) const
      -> decltype(std::forward<T>(X) & std::forward<U>(Y)) {
    return std::forward<T>(X) & std::forward<U>(Y);
  }
};

template <typename T = void> struct bit_or {
  constexpr T operator()(const T &X, const T &Y) const { return X | Y; }
};

template <> struct bit_or<void> {
  template <typename T, typename U>
  constexpr auto operator()(T &&X, U &&Y) const
      -> decltype(std::forward<T>(X) | std::forward<U>(Y)) {
    return std::forward<T>(X) | std::forward<U>(Y);
  }
};

template <typename T = void> struct bit_xor {
  constexpr T operator()(const T &X, const T &Y) const { return X ^ Y; }
};

template <> struct bit_xor<void> {
  template <typename T, typename U>
  constexpr auto operator()(T &&X, U &&Y) const
      -> decltype(std::forward<T>(X) ^ std::forward<U>(Y)) {
    return std::forward<T>(X) ^ std::forward<U>(Y);
  }
};

template <typename T = void> struct logical_and {
  constexpr T operator()(const T &X, const T &Y) const { return X && Y; }
};

template <> struct logical_and<void> {
  template <typename T, typename U>
  constexpr auto operator()(T &&X, U &&Y) const
      -> decltype(std::forward<T>(X) && std::forward<U>(Y)) {
    return std::forward<T>(X) && std::forward<U>(Y);
  }
};

template <typename T = void> struct logical_or {
  constexpr T operator()(const T &X, const T &Y) const { return X || Y; }
};

template <> struct logical_or<void> {
  template <typename T, typename U>
  constexpr auto operator()(T &&X, U &&Y) const
      -> decltype(std::forward<T>(X) || std::forward<U>(Y)) {
    return std::forward<T>(X) || std::forward<U>(Y);
  }
};

template <typename T = void> struct minimum {
  constexpr T operator()(const T &X, const T &Y) const { return Y < X ? Y : X; }
};

template <> struct minimum<void> {
  template <typename T, typename U>
  constexpr auto operator()(T &&X, U &&Y) const
      -> std::decay_t<decltype(false ? std::forward<T>(X) : std::forward<U>(Y))> {
    return Y < X ? std::forward<U>(Y) : std::forward<T>(X);
  }
};

template <typename T = void> struct maximum {
  constexpr T operator()(const T &X, const T &Y) const { return X < Y ? Y : X; }
};

template <> struct maximum<void> {
  template <typename T, typename U>
  constexpr auto operator()(T &&X, U &&Y) const
      -> std::decay_t<decltype(false ? std::forward<T>(X) : std::forward<U>(Y))> {
    return X < Y ? std::forward<U>(Y) : std::forward<T>(X);
  }
};

_LIBSYCL_END_NAMESPACE_SYCL

#endif // _LIBSYCL___IMPL_FUNCTIONAL_HPP
