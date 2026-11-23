//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_LOCAL_ACCESSOR_HPP
#define _LIBSYCL___IMPL_LOCAL_ACCESSOR_HPP

#include <sycl/__impl/buffer.hpp>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

template <typename T, int Dimensions = 1> class local_accessor {
  static_assert(Dimensions >= 1 && Dimensions <= 3,
                "Local accessors require one, two, or three dimensions");
  static_assert(std::is_trivially_copyable_v<T> &&
                    std::is_trivially_default_constructible_v<T> &&
                    std::is_trivially_destructible_v<T> &&
                    !std::is_const_v<T> && alignof(T) <= 16,
                "Local accessors require mutable trivial elements "
                "with alignment at most 16");
  using DecoratedElement = typename detail::DecoratedElement<
      T, access::address_space::local_space>::type;
  [[clang::annotate("sycl.local.pointer")]] DecoratedElement *MPointer =
      nullptr;
  range<Dimensions> MRange;

  static std::size_t checkedBytes(range<Dimensions> Range) {
    std::size_t Bytes = sizeof(T);
    for (int I = 0; I < Dimensions; ++I) {
      if (!Range[I] || Range[I] > 32768 / Bytes)
        throw exception(make_error_code(errc::invalid),
                        "A local accessor requires nonzero storage within "
                        "the 32768-byte workgroup limit");
      Bytes *= Range[I];
    }
    return Bytes;
  }

public:
  using value_type = T;
  using reference = T &;
  using const_reference = const T &;
  using size_type = std::size_t;
  static constexpr int dimensions = Dimensions;

  local_accessor() = default;
  local_accessor(range<Dimensions> Range, handler &Handler) : MRange(Range) {
#ifndef __SYCL_DEVICE_ONLY__
    MPointer = reinterpret_cast<T *>(Handler.requireLocal(checkedBytes(Range)));
#else
    (void)Handler;
#endif
  }
  range<Dimensions> get_range() const { return MRange; }
  std::size_t size() const { return MRange.size(); }
  std::size_t byte_size() const { return size() * sizeof(T); }
  bool empty() const { return size() == 0; }

  template <access::decorated Decoration>
  multi_ptr<T, access::address_space::local_space, Decoration>
  get_multi_ptr() const {
#ifndef __SYCL_DEVICE_ONLY__
    throw exception(make_error_code(errc::feature_not_supported),
                    "Local accessor memory is not host accessible");
#endif
    return multi_ptr<T, access::address_space::local_space, Decoration>(
        MPointer);
  }
  decltype(auto) operator[](std::size_t Index) const {
#ifndef __SYCL_DEVICE_ONLY__
    throw exception(make_error_code(errc::feature_not_supported),
                    "Local accessor memory is not host accessible");
#endif
    if constexpr (Dimensions == 1)
      return MPointer[Index];
    else
      return detail::AccessorSubscript<DecoratedElement, Dimensions, 1>(
          MPointer, MRange, Index);
  }
  decltype(auto) operator[](id<Dimensions> Index) const {
#ifndef __SYCL_DEVICE_ONLY__
    throw exception(make_error_code(errc::feature_not_supported),
                    "Local accessor memory is not host accessible");
#endif
    return (MPointer[detail::linearizeId(Index, MRange)]);
  }
};

_LIBSYCL_END_NAMESPACE_SYCL

#endif // _LIBSYCL___IMPL_LOCAL_ACCESSOR_HPP
