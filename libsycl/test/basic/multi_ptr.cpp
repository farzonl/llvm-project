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

using Space = sycl::access::address_space;
using Decoration = sycl::access::decorated;
using Global = sycl::decorated_global_ptr<int>;
using ConstGlobal = sycl::decorated_global_ptr<const int>;
using Generic =
    sycl::multi_ptr<const int, Space::generic_space, Decoration::yes>;

static_assert(std::is_convertible_v<Global, ConstGlobal>);
static_assert(!std::is_convertible_v<ConstGlobal, Global>);
static_assert(std::is_convertible_v<Global, Generic>);
static_assert(!std::is_convertible_v<Generic, Global>);
static_assert(!std::is_constructible_v<sycl::decorated_local_ptr<int>, Global>);
static_assert(
    !std::is_constructible_v<sycl::decorated_private_ptr<int>, Global>);
using ReadAccessor = sycl::accessor<int, 1, sycl::access_mode::read>;
static_assert(std::is_same_v<ReadAccessor::value_type, const int>);
static_assert(std::is_constructible_v<ConstGlobal, ReadAccessor>);
static_assert(!std::is_constructible_v<Global, ReadAccessor>);

#ifdef __SYCL_DEVICE_ONLY__
static_assert(!std::is_same_v<Global::pointer, int *>);
static_assert(
    !std::is_same_v<Global::pointer, sycl::decorated_local_ptr<int>::pointer>);
static_assert(!std::is_same_v<Global::pointer, Generic::pointer>);
static int __attribute__((opencl_local)) Shared[32];
#endif

void launchPointers(sycl::queue &Queue, sycl::buffer<int> &Input,
                    sycl::buffer<int> &Output) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::accessor Read(Input, Handler, sycl::read_only);
    sycl::accessor Write(Output, Handler, sycl::write_only);
    Handler.parallel_for<class MultiPtrResource>(
        sycl::nd_range<1>(sycl::range<1>(64), sycl::range<1>(32)),
        [=](sycl::nd_item<1> Item)
            __attribute__((reqd_work_group_size(32, 1, 1))) {
              std::ptrdiff_t I =
                  static_cast<std::ptrdiff_t>(Item.get_global_linear_id());
              ConstGlobal Begin(Read);
              auto Pointer = Begin + I;
              auto Old = Pointer++;
              --Pointer;
              Pointer += 1;
              auto End = Pointer--;
              Pointer -= 0;
              Generic GenericPointer(Pointer);
              sycl::raw_global_ptr<const int> Raw = Pointer;
              int PrivateValues[]{static_cast<int>(I), 7};
              sycl::decorated_private_ptr<int> Private(
                  (sycl::decorated_private_ptr<int>::pointer)PrivateValues);
              Private[0] += Private[1];
              auto Destination = Write.get_multi_ptr<Decoration::yes>() + I;
              *Destination =
                  *Pointer + GenericPointer[0] + Raw.get_raw()[0] + *Private;
              if (Old != Pointer || End - Pointer != 1)
                *Destination = -999;
            });
  });
}

void launchLocalPointers(sycl::queue &Queue, sycl::buffer<int> &Output,
                         sycl::buffer<int> &Scratch) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::accessor Write(Output, Handler, sycl::write_only);
    sycl::accessor GlobalScratch(Scratch, Handler);
    Handler.parallel_for<class MultiPtrLocal>(
        sycl::nd_range<1>(sycl::range<1>(64), sycl::range<1>(32)),
        [=](sycl::nd_item<1> Item)
            __attribute__((reqd_work_group_size(32, 1, 1))) {
              (void)Item;
              (void)Write;
              (void)GlobalScratch;
#ifdef __SYCL_DEVICE_ONLY__
              auto Local = sycl::decorated_local_ptr<int>(Shared);
              auto I = static_cast<std::ptrdiff_t>(Item.get_local_linear_id());
              Local[I] = static_cast<int>(Item.get_group_linear_id() * 100 + I);
              GlobalScratch[Item.get_global_linear_id()] = Local[I];
              sycl::group_barrier(Item.get_group());
              Write[Item.get_global_linear_id()] =
                  Local[(I + 1) % 32] +
                  GlobalScratch[Item.get_group_linear_id() * 32 + (I + 1) % 32];
#endif
            });
  });
}

#ifndef __SYCL_DEVICE_ONLY__
#  include <array>
#  include <cassert>
#  include <iostream>

int main(int Argc, char **Argv) {
  try {
    auto Devices = sycl::device::get_devices(sycl::info::device_type::gpu);
    auto Device = Devices.at(Argc > 1 ? std::stoul(Argv[1]) : 0);
    sycl::queue Queue(Device);
    std::array<int, 64> Input{}, Result{}, LocalResult{};
    for (unsigned I = 0; I < Input.size(); ++I)
      Input[I] = static_cast<int>(I * 17) - 103;
    {
      sycl::buffer<int> Source(Input.data(), sycl::range<1>(Input.size()));
      sycl::buffer<int> Destination(Result.data(),
                                    sycl::range<1>(Result.size()));
      sycl::buffer<int> LocalDestination(LocalResult.data(),
                                         sycl::range<1>(LocalResult.size()));
      sycl::buffer<int> Scratch(sycl::range<1>(LocalResult.size()));
      launchPointers(Queue, Source, Destination);
      launchLocalPointers(Queue, LocalDestination, Scratch);
    }
    for (unsigned I = 0; I < Input.size(); ++I) {
      assert(Result[I] == Input[I] * 3 + static_cast<int>(I) + 7);
      assert(LocalResult[I] ==
             2 * static_cast<int>((I / 32) * 100 + (I + 1) % 32));
    }
    int Values[]{3, 5, 7};
    sycl::raw_private_ptr<int> Pointer(Values);
    assert(*Pointer++ == 3 && *Pointer == 5);
    assert(Pointer > sycl::raw_private_ptr<int>(Values));
    assert(Pointer < sycl::raw_private_ptr<int>(Values + 2));
    Pointer = nullptr;
    assert(Pointer == nullptr && nullptr == Pointer);
    std::cout
        << Device.get_info<sycl::info::device::name>()
        << ": multi_ptr resource access and workgroup-local barriers passed\n";
    return 0;
  } catch (const sycl::exception &Error) {
    std::cerr << "multi_ptr test failed: " << Error.what() << '\n';
    return 1;
  }
}
#endif
