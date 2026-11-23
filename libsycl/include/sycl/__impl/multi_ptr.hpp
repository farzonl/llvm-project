//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_MULTI_PTR_HPP
#define _LIBSYCL___IMPL_MULTI_PTR_HPP

#include <sycl/__impl/access_enums.hpp>

#include <cstddef>
#include <iterator>
#include <type_traits>

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

_LIBSYCL_BEGIN_NAMESPACE_SYCL

template <typename, int, access_mode, target> class accessor;

namespace detail {
template <typename T, access::address_space Space> struct DecoratedElement {
  using type = T;
};

#ifdef __SYCL_DEVICE_ONLY__
template <typename T>
struct DecoratedElement<T, access::address_space::global_space> {
  using type = T [[clang::sycl_global]];
};
template <typename T>
struct DecoratedElement<T, access::address_space::local_space> {
  using type = T [[clang::sycl_local]];
};
template <typename T>
struct DecoratedElement<T, access::address_space::private_space> {
  using type = T [[clang::sycl_private]];
};
template <typename T>
struct DecoratedElement<T, access::address_space::generic_space> {
  using type = T [[clang::sycl_generic]];
};
#endif
} // namespace detail

template <typename ElementType, access::address_space Space,
          access::decorated DecorateAddress = access::decorated::legacy>
class multi_ptr {
  static_assert(DecorateAddress == access::decorated::yes ||
                    DecorateAddress == access::decorated::no,
                "The legacy multi_ptr interface is not implemented");
  static_assert(Space == access::address_space::global_space ||
                    Space == access::address_space::local_space ||
                    Space == access::address_space::private_space ||
                    Space == access::address_space::generic_space,
                "Constant-space multi_ptr is not implemented");
  static_assert(!std::is_void_v<ElementType>,
                "Void multi_ptr is not implemented");
  using decorated_element =
      typename detail::DecoratedElement<ElementType, Space>::type;
  using decorated_pointer = decorated_element *;
  decorated_pointer MPointer = nullptr;

public:
  using value_type = ElementType;
  static constexpr bool is_decorated =
      DecorateAddress == access::decorated::yes;
  static constexpr access::address_space address_space = Space;
  using pointer =
      std::conditional_t<is_decorated, decorated_pointer, ElementType *>;
  using reference =
      std::conditional_t<is_decorated, decorated_element &, ElementType &>;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::random_access_iterator_tag;

  multi_ptr() = default;
  multi_ptr(std::nullptr_t) {}
  explicit multi_ptr(decorated_pointer Pointer) : MPointer(Pointer) {}

  template <
      typename T, access::address_space OtherSpace,
      access::decorated Decoration,
      std::enable_if_t<std::is_convertible_v<T *, ElementType *> &&
                           (OtherSpace == Space ||
                            Space == access::address_space::generic_space),
                       int> = 0>
  multi_ptr(const multi_ptr<T, OtherSpace, Decoration> &Other)
      : MPointer((decorated_pointer)Other.get_decorated()) {}

  template <
      typename T, int Dimensions, access_mode Mode,
      std::enable_if_t<(Space == access::address_space::global_space ||
                        Space == access::address_space::generic_space) &&
                           std::is_convertible_v<
                               typename accessor<T, Dimensions, Mode,
                                                 target::device>::value_type *,
                               ElementType *>,
                       int> = 0>
  multi_ptr(const accessor<T, Dimensions, Mode, target::device> &Accessor)
      : MPointer((decorated_pointer)Accessor
                     .template get_multi_ptr<access::decorated::yes>()
                     .get_decorated()) {}

  multi_ptr &operator=(std::nullptr_t) {
    MPointer = nullptr;
    return *this;
  }
  pointer get() const { return (pointer)MPointer; }
  ElementType *get_raw() const { return (ElementType *)MPointer; }
  decorated_pointer get_decorated() const { return MPointer; }
  reference operator*() const { return *get(); }
  pointer operator->() const { return get(); }
  reference operator[](difference_type Index) const { return get()[Index]; }

  multi_ptr &operator++() { return *this += 1; }
  multi_ptr operator++(int) {
    auto Old = *this;
    ++*this;
    return Old;
  }
  multi_ptr &operator--() { return *this -= 1; }
  multi_ptr operator--(int) {
    auto Old = *this;
    --*this;
    return Old;
  }
  multi_ptr &operator+=(difference_type Offset) {
    MPointer += Offset;
    return *this;
  }
  multi_ptr &operator-=(difference_type Offset) {
    MPointer -= Offset;
    return *this;
  }
  friend multi_ptr operator+(multi_ptr Pointer, difference_type Offset) {
    return Pointer += Offset;
  }
  friend multi_ptr operator+(difference_type Offset, multi_ptr Pointer) {
    return Pointer += Offset;
  }
  friend multi_ptr operator-(multi_ptr Pointer, difference_type Offset) {
    return Pointer -= Offset;
  }
  friend difference_type operator-(multi_ptr Left, multi_ptr Right) {
    return Left.MPointer - Right.MPointer;
  }
  friend bool operator==(multi_ptr Left, multi_ptr Right) {
    return Left.MPointer == Right.MPointer;
  }
  friend bool operator!=(multi_ptr Left, multi_ptr Right) {
    return !(Left == Right);
  }
  friend bool operator<(multi_ptr Left, multi_ptr Right) {
    return Left.MPointer < Right.MPointer;
  }
  friend bool operator>(multi_ptr Left, multi_ptr Right) {
    return Right < Left;
  }
  friend bool operator<=(multi_ptr Left, multi_ptr Right) {
    return !(Right < Left);
  }
  friend bool operator>=(multi_ptr Left, multi_ptr Right) {
    return !(Left < Right);
  }
};

template <typename T, access::decorated Decoration = access::decorated::legacy>
using global_ptr =
    multi_ptr<T, access::address_space::global_space, Decoration>;
template <typename T, access::decorated Decoration = access::decorated::legacy>
using local_ptr = multi_ptr<T, access::address_space::local_space, Decoration>;
template <typename T, access::decorated Decoration = access::decorated::legacy>
using private_ptr =
    multi_ptr<T, access::address_space::private_space, Decoration>;

template <typename T>
using decorated_global_ptr = global_ptr<T, access::decorated::yes>;
template <typename T>
using decorated_local_ptr = local_ptr<T, access::decorated::yes>;
template <typename T>
using decorated_private_ptr = private_ptr<T, access::decorated::yes>;
template <typename T>
using raw_global_ptr = global_ptr<T, access::decorated::no>;
template <typename T> using raw_local_ptr = local_ptr<T, access::decorated::no>;
template <typename T>
using raw_private_ptr = private_ptr<T, access::decorated::no>;

_LIBSYCL_END_NAMESPACE_SYCL

#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

#endif // _LIBSYCL___IMPL_MULTI_PTR_HPP
