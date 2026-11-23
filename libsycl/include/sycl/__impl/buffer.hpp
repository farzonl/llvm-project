//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_BUFFER_HPP
#define _LIBSYCL___IMPL_BUFFER_HPP

#include <cstddef>
#include <limits>
#include <memory>
#include <sycl/__impl/access_enums.hpp>
#include <sycl/__impl/handler.hpp>
#include <type_traits>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

namespace detail {
class BufferImpl;
class HostAccessImpl;
_LIBSYCL_EXPORT BufferImpl *makeBuffer(void *Host, std::size_t Bytes);
_LIBSYCL_EXPORT std::shared_ptr<HostAccessImpl>
acquireHostAccess(BufferImpl *Buffer, access_mode Mode);
_LIBSYCL_EXPORT void *getHostAccessPointer(const HostAccessImpl &Access);
_LIBSYCL_EXPORT void retainBuffer(BufferImpl *Buffer) noexcept;
_LIBSYCL_EXPORT void releaseBuffer(BufferImpl *Buffer) noexcept;
_LIBSYCL_EXPORT void retainBufferOwner(BufferImpl *Buffer) noexcept;
_LIBSYCL_EXPORT void releaseBufferOwner(BufferImpl *Buffer) noexcept;
} // namespace detail

template <typename DataT, int Dimensions = 1,
          access_mode Mode = access_mode::read_write,
          target Target = target::device>
class accessor;

template <typename DataT, int Dimensions = 1,
          access_mode Mode = access_mode::read_write>
class host_accessor;

template <typename T, int Dimensions = 1,
          typename Allocator = std::allocator<T>>
class buffer {
  static_assert(Dimensions == 1,
                "Only one-dimensional buffers are implemented");
  static_assert(std::is_same_v<Allocator, std::allocator<T>>,
                "Custom buffer allocators are not implemented");
  static_assert(std::is_trivially_copyable_v<T> && !std::is_const_v<T>,
                "Buffers require mutable trivially copyable elements");
  static_assert(alignof(T) <= alignof(std::max_align_t),
                "Over-aligned buffer elements are not implemented");
  detail::BufferImpl *MImpl;
  range<Dimensions> MRange;
  template <typename, int, access_mode, target> friend class accessor;
  template <typename, int, access_mode> friend class host_accessor;

  static std::size_t checkedBytes(range<Dimensions> Range) {
    if (!Range.size() ||
        Range.size() > std::numeric_limits<std::size_t>::max() / sizeof(T))
      throw exception(make_error_code(errc::invalid),
                      "A buffer requires a nonempty representable size");
    return Range.size() * sizeof(T);
  }

public:
  explicit buffer(range<Dimensions> Range)
      : MImpl(detail::makeBuffer(nullptr, checkedBytes(Range))), MRange(Range) {
  }
  buffer(T *Host, range<Dimensions> Range) : MRange(Range) {
    if (!Host)
      throw exception(make_error_code(errc::invalid),
                      "A buffer requires valid nonempty host storage");
    MImpl = detail::makeBuffer(Host, checkedBytes(Range));
  }
  buffer(const buffer &Other) : MImpl(Other.MImpl), MRange(Other.MRange) {
    detail::retainBufferOwner(MImpl);
  }
  buffer(buffer &&Other) noexcept
      : MImpl(std::exchange(Other.MImpl, nullptr)), MRange(Other.MRange) {}
  buffer &operator=(const buffer &Other) {
    if (this != &Other) {
      detail::retainBufferOwner(Other.MImpl);
      detail::releaseBufferOwner(MImpl);
      MImpl = Other.MImpl;
      MRange = Other.MRange;
    }
    return *this;
  }
  buffer &operator=(buffer &&Other) noexcept {
    if (this != &Other) {
      detail::releaseBufferOwner(MImpl);
      MImpl = std::exchange(Other.MImpl, nullptr);
      MRange = Other.MRange;
    }
    return *this;
  }
  ~buffer() { detail::releaseBufferOwner(MImpl); }
  range<Dimensions> get_range() const { return MRange; }
  std::size_t size() const { return MRange.size(); }
  std::size_t byte_size() const { return size() * sizeof(T); }

  template <access_mode Mode = access_mode::read_write>
  accessor<T, Dimensions, Mode> get_access(handler &Handler) {
    return accessor<T, Dimensions, Mode>(*this, Handler);
  }
};

template <typename T, int Dimensions, access_mode Mode, target Target>
class accessor {
  static_assert(Dimensions == 1 && Target == target::device,
                "Only one-dimensional device accessors are implemented");
  static_assert(Mode == access_mode::read || Mode == access_mode::write ||
                    Mode == access_mode::read_write,
                "This accessor mode is not implemented");
  [[clang::annotate("sycl.accessor.pointer")]] T *MPointer;
  detail::BufferImpl *MBuffer;
  std::size_t MCount;

public:
  using value_type = T;
  using reference =
      std::conditional_t<Mode == access_mode::read, const T &, T &>;
  template <typename Allocator>
  accessor(buffer<T, Dimensions, Allocator> &Buffer, handler &Handler)
      : MPointer(static_cast<T *>(Handler.requireBuffer(Buffer.MImpl, Mode))),
        MBuffer(Buffer.MImpl), MCount(Buffer.size()) {
#ifndef __SYCL_DEVICE_ONLY__
    detail::retainBuffer(MBuffer);
#endif
  }
  template <typename Allocator>
  accessor(buffer<T, Dimensions, Allocator> &Buffer, handler &Handler,
           mode_tag_t<Mode>)
      : accessor(Buffer, Handler) {}
  accessor(const accessor &Other)
      : MPointer(Other.MPointer), MBuffer(Other.MBuffer), MCount(Other.MCount) {
#ifndef __SYCL_DEVICE_ONLY__
    detail::retainBuffer(MBuffer);
#endif
  }
  accessor &operator=(const accessor &Other) {
    if (this != &Other) {
#ifndef __SYCL_DEVICE_ONLY__
      detail::retainBuffer(Other.MBuffer);
      detail::releaseBuffer(MBuffer);
#endif
      MPointer = Other.MPointer;
      MBuffer = Other.MBuffer;
      MCount = Other.MCount;
    }
    return *this;
  }
  ~accessor() {
#ifndef __SYCL_DEVICE_ONLY__
    detail::releaseBuffer(MBuffer);
#endif
  }
  reference operator[](std::size_t Index) const { return MPointer[Index]; }
  reference operator[](id<Dimensions> Index) const { return (*this)[Index[0]]; }
  range<Dimensions> get_range() const { return range<Dimensions>(MCount); }
  std::size_t size() const { return MCount; }
};

template <typename T, int Dims, typename Allocator>
accessor(buffer<T, Dims, Allocator> &, handler &) -> accessor<T, Dims>;

template <typename T, int Dims, typename Allocator, access_mode Mode>
accessor(buffer<T, Dims, Allocator> &, handler &, mode_tag_t<Mode>)
    -> accessor<T, Dims, Mode>;

template <typename T, int Dimensions, access_mode Mode> class host_accessor {
  static_assert(Dimensions == 1,
                "Only one-dimensional host accessors are implemented");
  static_assert(Mode == access_mode::read || Mode == access_mode::write ||
                    Mode == access_mode::read_write,
                "This host accessor mode is not implemented");
  std::shared_ptr<detail::HostAccessImpl> MAccess;
  T *MPointer;
  std::size_t MCount;

public:
  using value_type = std::conditional_t<Mode == access_mode::read, const T, T>;
  using reference =
      std::conditional_t<Mode == access_mode::read, const T &, T &>;
  using pointer = std::conditional_t<Mode == access_mode::read, const T *, T *>;

  template <typename Allocator>
  explicit host_accessor(buffer<T, Dimensions, Allocator> &Buffer)
      : MAccess(detail::acquireHostAccess(Buffer.MImpl, Mode)),
        MPointer(static_cast<T *>(detail::getHostAccessPointer(*MAccess))),
        MCount(Buffer.size()) {}
  template <typename Allocator>
  host_accessor(buffer<T, Dimensions, Allocator> &Buffer, mode_tag_t<Mode>)
      : host_accessor(Buffer) {}

  reference operator[](std::size_t Index) const { return MPointer[Index]; }
  reference operator[](id<Dimensions> Index) const { return (*this)[Index[0]]; }
  pointer get_pointer() const { return MPointer; }
  range<Dimensions> get_range() const { return range<Dimensions>(MCount); }
  std::size_t size() const { return MCount; }
  std::size_t byte_size() const { return MCount * sizeof(T); }
};

template <typename T, int Dims, typename Allocator>
host_accessor(buffer<T, Dims, Allocator> &) -> host_accessor<T, Dims>;
template <typename T, int Dims, typename Allocator, access_mode Mode>
host_accessor(buffer<T, Dims, Allocator> &, mode_tag_t<Mode>)
    -> host_accessor<T, Dims, Mode>;

_LIBSYCL_END_NAMESPACE_SYCL

#endif // _LIBSYCL___IMPL_BUFFER_HPP
