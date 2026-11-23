//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_KERNEL_BUNDLE_HPP
#define _LIBSYCL___IMPL_KERNEL_BUNDLE_HPP

#include <algorithm>
#include <memory>
#include <string_view>
#include <sycl/__impl/context.hpp>
#include <type_traits>
#include <vector>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

enum class bundle_state { input, object, executable };

class kernel_id {
  std::shared_ptr<const std::string> impl;
  explicit kernel_id(std::shared_ptr<const std::string> Name)
      : impl(std::move(Name)) {}
  friend detail::ImplUtils;

public:
  const char *get_name() const { return impl->c_str(); }
  friend bool operator==(const kernel_id &X, const kernel_id &Y) {
    return *X.impl == *Y.impl;
  }
  friend bool operator!=(const kernel_id &X, const kernel_id &Y) {
    return !(X == Y);
  }
};

namespace detail {
class KernelImpl;
_LIBSYCL_EXPORT kernel_id makeKernelID(std::string_view Name);
} // namespace detail

namespace info::kernel {
struct function_name {
  using return_type = std::string;
};
} // namespace info::kernel

class _LIBSYCL_EXPORT kernel {
  std::shared_ptr<detail::KernelImpl> impl;
  explicit kernel(std::shared_ptr<detail::KernelImpl> Impl)
      : impl(std::move(Impl)) {}
  friend detail::ImplUtils;

public:
  context get_context() const;
  backend get_backend() const noexcept;
  std::string get_function_name() const;
  template <typename Param> typename Param::return_type get_info() const {
    static_assert(std::is_same_v<Param, info::kernel::function_name>,
                  "Kernel information descriptor is not supported");
    return get_function_name();
  }
  friend bool operator==(const kernel &X, const kernel &Y) {
    return X.get_context() == Y.get_context() &&
           X.get_function_name() == Y.get_function_name();
  }
  friend bool operator!=(const kernel &X, const kernel &Y) { return !(X == Y); }
};

namespace detail {
struct KernelBundleImpl {
  context Context;
  std::vector<device> Devices;
  std::vector<kernel_id> Kernels;
};
_LIBSYCL_EXPORT std::shared_ptr<KernelBundleImpl>
makeKernelBundle(bundle_state State, const context &Context,
                 const std::vector<device> &Devices,
                 const std::vector<kernel_id> &Kernels, bool SelectAll);
_LIBSYCL_EXPORT kernel makeKernel(const KernelBundleImpl &, const kernel_id &);
_LIBSYCL_EXPORT bool kernelIsCompatible(const kernel_id &, const device &);
} // namespace detail

template <bundle_state State> class kernel_bundle {
  std::shared_ptr<detail::KernelBundleImpl> impl;
  explicit kernel_bundle(std::shared_ptr<detail::KernelBundleImpl> Impl)
      : impl(std::move(Impl)) {}
  friend detail::ImplUtils;

public:
  context get_context() const { return impl->Context; }
  backend get_backend() const noexcept { return impl->Context.get_backend(); }
  std::vector<device> get_devices() const { return impl->Devices; }
  std::vector<kernel_id> get_kernel_ids() const { return impl->Kernels; }
  bool empty() const { return impl->Kernels.empty(); }
  bool has_kernel(const kernel_id &ID) const {
    return std::find(impl->Kernels.begin(), impl->Kernels.end(), ID) !=
           impl->Kernels.end();
  }
  bool has_kernel(const kernel_id &ID, const device &Device) const {
    return has_kernel(ID) &&
           std::find(impl->Devices.begin(), impl->Devices.end(), Device) !=
               impl->Devices.end() &&
           detail::kernelIsCompatible(ID, Device);
  }
  kernel get_kernel(const kernel_id &ID) const {
    static_assert(State == bundle_state::executable,
                  "Only executable bundles can provide kernels");
    return detail::makeKernel(*impl, ID);
  }
  friend bool operator==(const kernel_bundle &X, const kernel_bundle &Y) {
    return X.impl == Y.impl;
  }
  friend bool operator!=(const kernel_bundle &X, const kernel_bundle &Y) {
    return !(X == Y);
  }
};

template <typename KernelName> kernel_id get_kernel_id() {
#ifdef SYCL_LANGUAGE_VERSION
  return detail::makeKernelID(__builtin_sycl_unique_stable_name(KernelName));
#else
  static_assert(!std::is_same_v<KernelName, KernelName>,
                "get_kernel_id<KernelName> requires compilation with -fsycl");
#endif
}

_LIBSYCL_EXPORT std::vector<kernel_id> get_kernel_ids();

template <bundle_state State>
kernel_bundle<State> get_kernel_bundle(const context &Context) {
  return detail::createSyclObjFromImpl<kernel_bundle<State>>(
      detail::makeKernelBundle(State, Context, Context.get_devices(), {},
                               true));
}

template <bundle_state State>
kernel_bundle<State> get_kernel_bundle(const context &Context,
                                       const std::vector<device> &Devices) {
  return detail::createSyclObjFromImpl<kernel_bundle<State>>(
      detail::makeKernelBundle(State, Context, Devices, {}, true));
}

template <bundle_state State>
kernel_bundle<State> get_kernel_bundle(const context &Context,
                                       const std::vector<device> &Devices,
                                       const std::vector<kernel_id> &Kernels) {
  return detail::createSyclObjFromImpl<kernel_bundle<State>>(
      detail::makeKernelBundle(State, Context, Devices, Kernels, false));
}

template <typename KernelName, bundle_state State>
kernel_bundle<State> get_kernel_bundle(const context &Context) {
  return get_kernel_bundle<State>(Context, Context.get_devices(),
                                  {get_kernel_id<KernelName>()});
}

_LIBSYCL_END_NAMESPACE_SYCL

namespace std {
template <> struct hash<sycl::kernel_id> {
  size_t operator()(const sycl::kernel_id &ID) const {
    return hash<string_view>{}(ID.get_name());
  }
};
template <> struct hash<sycl::kernel> {
  size_t operator()(const sycl::kernel &Kernel) const {
    return hash<sycl::context>{}(Kernel.get_context()) ^
           (hash<string>{}(Kernel.get_function_name()) << 1);
  }
};
template <sycl::bundle_state State>
struct hash<sycl::kernel_bundle<State>>
    : sycl::detail::HashBase<sycl::kernel_bundle<State>> {};
} // namespace std

#endif // _LIBSYCL___IMPL_KERNEL_BUNDLE_HPP
