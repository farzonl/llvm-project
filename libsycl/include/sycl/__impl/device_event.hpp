//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_DEVICE_EVENT_HPP
#define _LIBSYCL___IMPL_DEVICE_EVENT_HPP

#include <sycl/__impl/detail/config.hpp>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

template <int> class group;

namespace detail {
void unsupportedDeviceEventWait() noexcept
    __attribute__((error("Device event waits are currently supported only in "
                         "DirectX SYCL device code")));
[[noreturn]] void unsupportedWorkGroupCopy()
    __attribute__((error("Workgroup copies are currently supported only in "
                         "DirectX SYCL device code")));
} // namespace detail

class device_event {
  explicit device_event() = default;
  template <int> friend class group;

public:
  void wait() noexcept {
#if !defined(__SYCL_DEVICE_ONLY__) || !defined(__DIRECTX__)
    detail::unsupportedDeviceEventWait();
#endif
    // Copies complete collectively before construction. Waiting must not add
    // another collective: an already-completed event can be waited on alone.
  }
};

_LIBSYCL_END_NAMESPACE_SYCL

#endif // _LIBSYCL___IMPL_DEVICE_EVENT_HPP
