//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_ACCESS_ENUMS_HPP
#define _LIBSYCL___IMPL_ACCESS_ENUMS_HPP

#include <sycl/__impl/detail/config.hpp>

#include <cstdint>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

enum class access_mode : std::uint32_t {
  read,
  write,
  read_write,
  discard_write __SYCL2020_DEPRECATED("Use write with property::no_init instead"),
  discard_read_write
      __SYCL2020_DEPRECATED("Use read_write with property::no_init instead"),
  atomic __SYCL2020_DEPRECATED("Use atomic_ref instead")
};

enum class target : std::uint32_t {
  device,
  host_task,
  constant_buffer __SYCL2020_DEPRECATED("Use a read-only accessor instead"),
  local __SYCL2020_DEPRECATED("Use local_accessor instead"),
  host_buffer __SYCL2020_DEPRECATED("Use host_accessor instead"),
  global_buffer __SYCL2020_DEPRECATED("Use target::device instead") = device
};

template <access_mode Mode> struct mode_tag_t {};
inline constexpr mode_tag_t<access_mode::read> read_only{};
inline constexpr mode_tag_t<access_mode::write> write_only{};
inline constexpr mode_tag_t<access_mode::read_write> read_write{};

namespace access {

using mode __SYCL2020_DEPRECATED("Use sycl::access_mode instead") = access_mode;
using target __SYCL2020_DEPRECATED("Use sycl::target instead") = sycl::target;

enum class decorated : std::uint32_t {
  no,
  yes,
  legacy __SYCL2020_DEPRECATED("Use decorated::yes or decorated::no instead")
};

enum class address_space : std::uint32_t {
  global_space,
  local_space,
  private_space,
  generic_space,
  constant_space
      __SYCL2020_DEPRECATED("Use global_space with a const element type instead")
};

enum class fence_space : std::uint32_t {
  local_space,
  global_space,
  global_and_local
};

} // namespace access

_LIBSYCL_END_NAMESPACE_SYCL

#endif // _LIBSYCL___IMPL_ACCESS_ENUMS_HPP
