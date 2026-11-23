//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains helpers for kernel submission entry points.
///
//===----------------------------------------------------------------------===//

#ifndef _LIBSYCL___IMPL_DETAIL_KERNEL_SUBMISSION_HPP
#define _LIBSYCL___IMPL_DETAIL_KERNEL_SUBMISSION_HPP

#include <sycl/__impl/detail/config.hpp>
#include <sycl/__impl/detail/get_device_kernel_info.hpp>
#include <sycl/__impl/detail/kernel_arg_helpers.hpp>
#include <sycl/__impl/exception.hpp>
#include <sycl/__impl/index_space_classes.hpp>
#include <sycl/__impl/nd_item.hpp>
#include <sycl/__impl/nd_range.hpp>

#include <tuple>
#include <type_traits>
#include <utility>

_LIBSYCL_BEGIN_NAMESPACE_SYCL

namespace detail {

template <int Dims>
void checkNDRangeAndThrow(const sycl::nd_range<Dims> &ExecutionRange) {
  if (ExecutionRange.get_global_range() != range<Dims>{} &&
      (ExecutionRange.get_local_range().size() == 0 ||
       ExecutionRange.get_global_range() % ExecutionRange.get_local_range() !=
           range<Dims>{}))
    throw sycl::exception(sycl::make_error_code(sycl::errc::nd_range),
                          "Invalid nd_range submission: global size must be "
                          "evenly divisible by local size.");
}

template <typename KernelType, typename ElementType, int Dims>
struct RangeKernelWrapper {
  KernelType KernelFunc;
  range<Dims> LogicalRange;

  void operator()(ElementType NativeItem) const {
#if defined(__SYCL_DEVICE_ONLY__) && defined(__DIRECTX__)
    const id<Dims> Offset = __spirv::initBuiltInGlobalOffset<Dims, id<Dims>>();
    const id<Dims> Start = NativeItem.get_id() - Offset;
    const range<Dims> Stride =
        __spirv::initBuiltInGlobalSize<Dims, range<Dims>>();
    id<Dims> Index{};
    invoke<0>(Index, Start, Stride, Offset);
#else
    KernelFunc(NativeItem);
#endif
  }

#if defined(__SYCL_DEVICE_ONLY__) && defined(__DIRECTX__)
private:
  template <int Axis>
  void invoke(id<Dims> &Index, const id<Dims> &Start, const range<Dims> &Stride,
              const id<Dims> &Offset) const {
    for (Index[Axis] = Start[Axis]; Index[Axis] < LogicalRange[Axis];) {
      if constexpr (Axis + 1 < Dims) {
        invoke<Axis + 1>(Index, Start, Stride, Offset);
      } else if constexpr (std::is_same_v<ElementType, item<Dims>>) {
        KernelFunc(Builder::createItem<Dims, true>(LogicalRange, Index + Offset,
                                                   Offset));
      } else {
        KernelFunc(Builder::createItem<Dims, false>(LogicalRange, Index));
      }
      // Test the remaining distance before adding to avoid size_t overflow.
      if (LogicalRange[Axis] - Index[Axis] <= Stride[Axis])
        break;
      Index[Axis] += Stride[Axis];
    }
  }
#endif
};

template <typename DerivedT> class KernelSubmissionBase {
protected:
  template <typename KernelName, typename KernelType>
#ifdef SYCL_LANGUAGE_VERSION
  [[clang::sycl_kernel_entry_point(KernelName)]]
#endif
  void submitSingleTask(const KernelType &KernelFunc) {
    KernelFunc();
  }

  template <typename KernelName, typename ElementType, typename KernelType>
#ifdef SYCL_LANGUAGE_VERSION
  [[clang::sycl_kernel_entry_point(KernelName)]]
#endif
  void submitParallelFor(const KernelType &KernelFunc) {
    KernelFunc(detail::Builder::getElement(detail::declptr<ElementType>()));
  }

  template <typename KN, typename... Args>
  void sycl_kernel_launch(const char *KernelName, Args &&...KernelArgs) {
    static_assert(
        sizeof...(KernelArgs) == 1,
        "sycl_kernel_launch expects only 2 arguments now: name of kernel and "
        "callable object passed to kernel invocation by the user.");

    auto FirstArg = std::get<0>(std::tie(KernelArgs...));
    static_cast<DerivedT *>(this)->submitKernelImpl(
        detail::getDeviceKernelInfo<KN>(KernelName), &FirstArg,
        sizeof(FirstArg));
  }

  template <typename KernelName, int Dims, template <int> class Range,
            typename... Rest>
  void parallelForImpl(const Range<Dims> &NumWorkItems, Rest &&...RestArgs) {
    if constexpr (sizeof...(Rest) != 1)
      throw sycl::exception(sycl::make_error_code(errc::feature_not_supported),
                            "Reductions are not supported");

    using KernelType =
        std::decay_t<detail::nth_type_t<sizeof...(Rest) - 1, Rest...>>;
    constexpr bool IsNdRangeSubmission =
        std::is_same_v<Range<Dims>, nd_range<Dims>>;
    using SuggestedArgType =
        std::conditional_t<IsNdRangeSubmission, nd_item<Dims>, item<Dims>>;
    using LambdaArgType =
        sycl::detail::lambda_arg_type<KernelType, SuggestedArgType>;

    if constexpr (IsNdRangeSubmission) {
      static_assert(
          std::is_convertible_v<sycl::nd_item<Dims>, LambdaArgType>,
          "Kernel argument of a sycl::parallel_for with sycl::nd_range "
          "must be sycl::nd_item or be convertible from sycl::nd_item");
    } else {
      static_assert(
          std::is_convertible_v<sycl::item<Dims>, LambdaArgType> ||
              std::is_convertible_v<sycl::item<Dims, false>, LambdaArgType>,
          "Kernel argument of a sycl::parallel_for with sycl::range "
          "must be sycl::item or be convertible from sycl::item");
    }

    using TransformedLambdaArgType = std::conditional_t<
        IsNdRangeSubmission, nd_item<Dims>,
        std::conditional_t<
            std::is_convertible_v<sycl::item<Dims>, LambdaArgType>, item<Dims>,
            std::conditional_t<
                std::is_convertible_v<sycl::item<Dims, false>, LambdaArgType>,
                item<Dims, false>, LambdaArgType>>>;

    using NameT =
        typename detail::get_kernel_name_t<KernelName, KernelType>::name;
    if constexpr (IsNdRangeSubmission) {
      return submitParallelFor<NameT, TransformedLambdaArgType, KernelType>(
          std::forward<Rest>(RestArgs)...);
    } else {
      using Wrapper =
          RangeKernelWrapper<KernelType, TransformedLambdaArgType, Dims>;
      return submitParallelFor<NameT, TransformedLambdaArgType, Wrapper>(
          Wrapper{std::forward<Rest>(RestArgs)..., NumWorkItems});
    }
  }
};

} // namespace detail

_LIBSYCL_END_NAMESPACE_SYCL

#endif // _LIBSYCL___IMPL_DETAIL_KERNEL_SUBMISSION_HPP
