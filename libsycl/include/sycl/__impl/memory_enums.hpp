//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains SYCL 2020 memory order and scope enumerations.
///
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_MEMORY_ENUMS_HPP
#define _LIBSYCL___IMPL_MEMORY_ENUMS_HPP

#include <sycl/__impl/detail/config.hpp>

#include <cstdint>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

enum class memory_order : std::uint32_t {
  relaxed,
  acquire,
  release,
  acq_rel,
  seq_cst
};

inline constexpr auto memory_order_relaxed = memory_order::relaxed;
inline constexpr auto memory_order_acquire = memory_order::acquire;
inline constexpr auto memory_order_release = memory_order::release;
inline constexpr auto memory_order_acq_rel = memory_order::acq_rel;
inline constexpr auto memory_order_seq_cst = memory_order::seq_cst;

enum class memory_scope : std::uint32_t {
  work_item = 0,
  sub_group = 1,
  work_group = 2,
  device = 3,
  system = 4
};

inline constexpr auto memory_scope_work_item = memory_scope::work_item;
inline constexpr auto memory_scope_sub_group = memory_scope::sub_group;
inline constexpr auto memory_scope_work_group = memory_scope::work_group;
inline constexpr auto memory_scope_device = memory_scope::device;
inline constexpr auto memory_scope_system = memory_scope::system;

_LIBSYCL_END_NAMESPACE_SYCL

#endif // _LIBSYCL___IMPL_MEMORY_ENUMS_HPP
