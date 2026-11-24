//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// REQUIRES: any-device
// RUN: %clangxx -fsycl %s -o %t.out
// RUN: %t.out

#include <sycl/sycl.hpp>

template <typename Owner, typename Destination, typename Source,
          typename = void>
struct CanCopy : std::false_type {};
template <typename Owner, typename Destination, typename Source>
struct CanCopy<
    Owner, Destination, Source,
    std::void_t<decltype(std::declval<const Owner &>().async_work_group_copy(
        std::declval<Destination>(), std::declval<Source>(), 1))>>
    : std::true_type {};

using Local = sycl::decorated_local_ptr<int>;
using Global = sycl::decorated_global_ptr<int>;
using ConstGlobal = sycl::decorated_global_ptr<const int>;
static_assert(CanCopy<sycl::group<1>, Local, ConstGlobal>::value);
static_assert(CanCopy<sycl::nd_item<1>, Global, Local>::value);
static_assert(!CanCopy<sycl::group<1>, Global, ConstGlobal>::value);
static_assert(!CanCopy<sycl::group<1>, sycl::decorated_local_ptr<const int>,
                       Global>::value);
static_assert(!CanCopy<sycl::nd_item<1>, sycl::decorated_local_ptr<float>,
                       Global>::value);
static_assert(noexcept(std::declval<sycl::device_event &>().wait()));
static_assert(!std::is_default_constructible_v<sycl::device_event>);

template <typename T, int D> class WorkgroupCopyKernel;
template <typename T> struct CopyElement {
  using type = T;
};
template <typename T, int N> struct CopyElement<sycl::vec<T, N>> {
  using type = T;
};
template <typename T, std::size_t N> struct CopyElement<sycl::marray<T, N>> {
  using type = T;
};
template <typename T>
constexpr bool IsWideCopy =
    std::is_same_v<typename CopyElement<T>::type, long long>;

template <typename T, int D>
void launchCopy(sycl::queue &Queue, sycl::buffer<T> &Input,
                sycl::buffer<T> &Output, sycl::nd_range<D> Range,
                std::size_t Count, std::size_t SourceStride,
                std::size_t DestinationStride, std::size_t Span) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::local_accessor<T> First(sycl::range<1>(Count + 2), Handler);
    sycl::local_accessor<T> Second(sycl::range<1>(Count + 2), Handler);
    sycl::accessor Read(Input, Handler, sycl::read_only);
    sycl::accessor Write(Output, Handler);
    Handler.parallel_for<WorkgroupCopyKernel<T, D>>(Range, [=](sycl::nd_item<D>
                                                                   Item) {
      const auto Group = Item.get_group();
      auto I = Item.get_local_linear_id();
      auto Step = Group.get_local_linear_range();
      auto Base =
          static_cast<std::ptrdiff_t>(4 + Item.get_group_linear_id() * Span);
      auto A = First.template get_multi_ptr<sycl::access::decorated::yes>();
      auto B = Second.template get_multi_ptr<sycl::access::decorated::yes>();
      auto Source =
          Read.template get_multi_ptr<sycl::access::decorated::yes>() + Base;
      auto Destination =
          Write.template get_multi_ptr<sycl::access::decorated::yes>() + Base;
      if (I == 0) {
        A[0] = A[Count + 1] = T(12345);
        B[0] = B[Count + 1] = T(23456);
      }
      auto E1 =
          SourceStride == 1
              ? Group.async_work_group_copy(A + 1, Source, Count)
              : Group.async_work_group_copy(A + 1, Source, Count, SourceStride);
      auto E2 =
          SourceStride == 1
              ? Item.async_work_group_copy(B + 1, Source, Count)
              : Item.async_work_group_copy(B + 1, Source, Count, SourceStride);
      Group.wait_for(E1, E2);
      if (I == 0)
        E1.wait();
      for (std::size_t Index = I; Index < Count; Index += Step) {
        if constexpr (IsWideCopy<T> || sizeof(T) == 1) {
          std::size_t Next = Index + 1 == Count ? 0 : Index + 1;
          A[Index + 1] = A[Index + 1] + T(3) * B[Next + 1] + T(7);
        } else
          A[Index + 1] = A[Index + 1] + B[Index + 1] + T(7);
      }
      sycl::decorated_local_ptr<const T> ConstA = A + 1;
      auto Scatter =
          DestinationStride == 1
              ? Item.async_work_group_copy(Destination, ConstA, Count)
              : Item.async_work_group_copy(Destination, ConstA, Count,
                                           DestinationStride);
      sycl::decorated_global_ptr<const T> ConstDestination = Destination;
      auto Back = Group.async_work_group_copy(B + 1, ConstDestination, Count,
                                              DestinationStride);
      Item.wait_for(Scatter, Back, E1);
      sycl::decorated_local_ptr<const T> ConstB = B + 1;
      auto RoundTrip = Destination + Count * DestinationStride + 4;
      auto Final = Group.async_work_group_copy(RoundTrip, ConstB, Count);
      Final.wait();
      Group.wait_for();
      if (I == 0) {
        if constexpr (sycl::detail::IsValueArray<T>::value) {
          for (std::size_t C = 0; C < T::size(); ++C)
            if (A[0][C] != 12345 || A[Count + 1][C] != 12345 ||
                B[0][C] != 23456 || B[Count + 1][C] != 23456)
              Destination[-1][C] = -999;
        } else if (A[0] != T(12345) || A[Count + 1] != T(12345) ||
                   B[0] != T(23456) || B[Count + 1] != T(23456))
          Destination[-1] = T(-999);
      }
    });
  });
}

template void launchCopy<int, 1>(sycl::queue &, sycl::buffer<int> &,
                                 sycl::buffer<int> &, sycl::nd_range<1>,
                                 std::size_t, std::size_t, std::size_t,
                                 std::size_t);
template void launchCopy<int, 2>(sycl::queue &, sycl::buffer<int> &,
                                 sycl::buffer<int> &, sycl::nd_range<2>,
                                 std::size_t, std::size_t, std::size_t,
                                 std::size_t);
template void launchCopy<float, 3>(sycl::queue &, sycl::buffer<float> &,
                                   sycl::buffer<float> &, sycl::nd_range<3>,
                                   std::size_t, std::size_t, std::size_t,
                                   std::size_t);
template void launchCopy<long long, 1>(sycl::queue &, sycl::buffer<long long> &,
                                       sycl::buffer<long long> &,
                                       sycl::nd_range<1>, std::size_t,
                                       std::size_t, std::size_t, std::size_t);
template void launchCopy<sycl::vec<int, 4>, 1>(
    sycl::queue &, sycl::buffer<sycl::vec<int, 4>> &,
    sycl::buffer<sycl::vec<int, 4>> &, sycl::nd_range<1>, std::size_t,
    std::size_t, std::size_t, std::size_t);
template void launchCopy<sycl::vec<float, 3>, 2>(
    sycl::queue &, sycl::buffer<sycl::vec<float, 3>> &,
    sycl::buffer<sycl::vec<float, 3>> &, sycl::nd_range<2>, std::size_t,
    std::size_t, std::size_t, std::size_t);
template void launchCopy<sycl::vec<long long, 2>, 1>(
    sycl::queue &, sycl::buffer<sycl::vec<long long, 2>> &,
    sycl::buffer<sycl::vec<long long, 2>> &, sycl::nd_range<1>, std::size_t,
    std::size_t, std::size_t, std::size_t);

template void launchCopy<sycl::marray<int, 3>, 1>(
    sycl::queue &, sycl::buffer<sycl::marray<int, 3>> &,
    sycl::buffer<sycl::marray<int, 3>> &, sycl::nd_range<1>, std::size_t,
    std::size_t, std::size_t, std::size_t);

template void launchCopy<sycl::vec<int, 8>, 1>(
    sycl::queue &, sycl::buffer<sycl::vec<int, 8>> &,
    sycl::buffer<sycl::vec<int, 8>> &, sycl::nd_range<1>, std::size_t,
    std::size_t, std::size_t, std::size_t);
template void launchCopy<sycl::vec<float, 16>, 2>(
    sycl::queue &, sycl::buffer<sycl::vec<float, 16>> &,
    sycl::buffer<sycl::vec<float, 16>> &, sycl::nd_range<2>, std::size_t,
    std::size_t, std::size_t, std::size_t);
template void launchCopy<sycl::vec<long long, 16>, 1>(
    sycl::queue &, sycl::buffer<sycl::vec<long long, 16>> &,
    sycl::buffer<sycl::vec<long long, 16>> &, sycl::nd_range<1>, std::size_t,
    std::size_t, std::size_t, std::size_t);

template void launchCopy<signed char, 1>(sycl::queue &,
                                         sycl::buffer<signed char> &,
                                         sycl::buffer<signed char> &,
                                         sycl::nd_range<1>, std::size_t,
                                         std::size_t, std::size_t, std::size_t);
template void launchCopy<unsigned char, 2>(
    sycl::queue &, sycl::buffer<unsigned char> &, sycl::buffer<unsigned char> &,
    sycl::nd_range<2>, std::size_t, std::size_t, std::size_t, std::size_t);

#ifndef __SYCL_DEVICE_ONLY__
#  include <cassert>
#  include <iostream>
#  include <vector>

template <typename T>
void verifyCopy(const std::vector<T> &Actual, const std::vector<T> &Expected) {
  assert(Actual.size() == Expected.size());
  for (std::size_t I = 0; I < Actual.size(); ++I)
    if constexpr (sycl::detail::IsValueArray<T>::value) {
      for (std::size_t C = 0; C < T::size(); ++C)
        assert(Actual[I][C] == Expected[I][C]);
    } else {
      if (Actual[I] != Expected[I])
        std::cerr << "copy mismatch at " << I << ": actual " << +Actual[I]
                  << ", expected " << +Expected[I] << '\n';
      assert(Actual[I] == Expected[I]);
    }
}

template <typename T, int D>
void checkCopy(sycl::queue &Queue, sycl::range<D> LocalRange) {
  auto GlobalRange = LocalRange;
  GlobalRange[0] *= 3;
  for (std::size_t Count :
       {std::size_t(0), std::size_t(1), LocalRange.size() - 1,
        LocalRange.size() + 3,
        std::size_t(IsWideCopy<T> || sycl::detail::IsValueArray<T>::value ||
                            sizeof(T) == 1
                        ? 32768 / (2 * sizeof(T)) - 2
                        : 257)}) {
    for (auto Strides : {sycl::range<2>(1, 1), sycl::range<2>(3, 5)}) {
      std::size_t Span = Count * (Strides[0] + Strides[1] + 2) + 16;
      std::vector<T> Input(Span * 3 + 8);
      std::vector<T> Output(Input.size(), T(-1234));
      auto Expected = Output;
      for (std::size_t I = 0; I < Input.size(); ++I) {
        if constexpr (sycl::detail::IsValueArray<T>::value) {
          for (std::size_t C = 0; C < T::size(); ++C) {
            if constexpr (IsWideCopy<T>) {
              long long Value = 0x1234567887654300LL + (I + C * 11) % 137;
              Input[I][C] = (I + C) % 2 ? -Value : Value;
            } else
              Input[I][C] = static_cast<typename T::value_type>(
                  static_cast<int>((I + C * 11) % 137) - 70);
          }
        } else if constexpr (IsWideCopy<T>) {
          long long Value = 0x1234567887654300LL + I % 137;
          Input[I] = I % 2 ? -Value : Value;
        } else if constexpr (sizeof(T) == 1) {
          Input[I] = T(static_cast<unsigned char>(I * 37 + 19));
        } else
          Input[I] = T(static_cast<int>(I % 137) - 70);
      }
      auto Original = Input;
      for (std::size_t Group = 0; Group < 3; ++Group) {
        std::size_t Base = 4 + Group * Span;
        for (std::size_t I = 0; I < Count; ++I) {
          T Value;
          if constexpr (IsWideCopy<T> || sizeof(T) == 1) {
            std::size_t Next = I + 1 == Count ? 0 : I + 1;
            Value = Input[Base + I * Strides[0]] +
                    T(3) * Input[Base + Next * Strides[0]] + T(7);
          } else
            Value = T(2) * Input[Base + I * Strides[0]] + T(7);
          Expected[Base + I * Strides[1]] = Value;
          Expected[Base + Count * Strides[1] + 4 + I] = Value;
        }
      }
      {
        sycl::buffer<T> Source(Input.data(), sycl::range<1>(Input.size()));
        sycl::buffer<T> Destination(Output.data(),
                                    sycl::range<1>(Output.size()));
        launchCopy(Queue, Source, Destination,
                   sycl::nd_range<D>(GlobalRange, LocalRange), Count,
                   Strides[0], Strides[1], Span);
      }
      verifyCopy(Input, Original);
      verifyCopy(Output, Expected);
    }
  }
}

int main(int Argc, char **Argv) {
  try {
    auto Devices = sycl::device::get_devices(sycl::info::device_type::gpu);
    auto Device = Devices.at(Argc > 1 ? std::stoul(Argv[1]) : 0);
    sycl::queue Queue(Device);
    checkCopy<int>(Queue, sycl::range<1>(8));
    checkCopy<int>(Queue, sycl::range<2>(2, 3));
    checkCopy<float>(Queue, sycl::range<3>(2, 3, 2));
    checkCopy<long long>(Queue, sycl::range<1>(8));
    checkCopy<sycl::vec<int, 4>>(Queue, sycl::range<1>(8));
    checkCopy<sycl::vec<float, 3>>(Queue, sycl::range<2>(2, 3));
    checkCopy<sycl::vec<long long, 2>>(Queue, sycl::range<1>(8));
    checkCopy<sycl::marray<int, 3>>(Queue, sycl::range<1>(8));
    checkCopy<sycl::vec<int, 8>>(Queue, sycl::range<1>(8));
    checkCopy<sycl::vec<float, 16>>(Queue, sycl::range<2>(2, 3));
    checkCopy<sycl::vec<long long, 16>>(Queue, sycl::range<1>(8));
    checkCopy<signed char>(Queue, sycl::range<1>(32));
    checkCopy<unsigned char>(Queue, sycl::range<2>(4, 8));
    std::cout
        << Device.get_info<sycl::info::device::name>()
        << ": scalar/vector cooperative copies, strides, guards and event "
           "waits passed\n";
    return 0;
  } catch (const sycl::exception &Error) {
    std::cerr << "workgroup copy test failed: " << Error.what() << '\n';
    return 1;
  }
}
#endif
