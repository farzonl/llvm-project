//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_BIT_CAST_HPP
#define _LIBSYCL___IMPL_BIT_CAST_HPP

#include <sycl/__impl/detail/config.hpp>
#include <type_traits>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

template <typename To, typename From>
constexpr std::enable_if_t<
    sizeof(To) == sizeof(From) && std::is_trivially_copyable_v<To> &&
        std::is_trivially_copyable_v<From>,
    To>
bit_cast(const From &Value) noexcept {
  return __builtin_bit_cast(To, Value);
}

_LIBSYCL_END_NAMESPACE_SYCL

#endif // _LIBSYCL___IMPL_BIT_CAST_HPP
