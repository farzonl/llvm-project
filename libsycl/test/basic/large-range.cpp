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

template <int D, bool UseHandler> class LogicalRange;
class WideLogicalRange;

template <int D, bool UseHandler>
void launch(sycl::queue &Queue, unsigned *Output, sycl::range<D> Extent) {
  auto Kernel = [=](sycl::item<D> Item) {
    unsigned Value = static_cast<unsigned>(Item.get_linear_id() + 1);
    for (int Axis = 0; Axis < D; ++Axis)
      Value += static_cast<unsigned>(Item.get_id(Axis) * (Axis + 7) +
                                     Item.get_range(Axis));
    Output[Item.get_linear_id()] = Value;
  };
  if constexpr (UseHandler)
    Queue
        .submit([&](sycl::handler &Handler) {
          Handler.parallel_for<LogicalRange<D, UseHandler>>(Extent, Kernel);
        })
        .wait_and_throw();
  else
    Queue.parallel_for<LogicalRange<D, UseHandler>>(Extent, Kernel)
        .wait_and_throw();
}

template void launch<1, false>(sycl::queue &, unsigned *, sycl::range<1>);
template void launch<2, true>(sycl::queue &, unsigned *, sycl::range<2>);
template void launch<3, false>(sycl::queue &, unsigned *, sycl::range<3>);

void launchWide(sycl::queue &Queue, unsigned *Output, size_t Count) {
  Queue
      .parallel_for<WideLogicalRange>(
          sycl::range<1>{Count},
          [=](sycl::item<1, false> Item) {
            if (Item.get_id(0) == Count - 1) {
              Output[0] = static_cast<unsigned>(Item.get_id(0));
              Output[1] = static_cast<unsigned>(Item.get_id(0) >> 32);
              Output[2] = static_cast<unsigned>(Item.get_range(0));
              Output[3] = static_cast<unsigned>(Item.get_range(0) >> 32);
              Output[4] = static_cast<unsigned>(Item.get_linear_id());
              Output[5] = static_cast<unsigned>(Item.get_linear_id() >> 32);
            }
          })
      .wait_and_throw();
}

#ifndef __SYCL_DEVICE_ONLY__
#  include <cassert>
#  include <iostream>
#  include <string>
#  include <vector>

template <int D, bool UseHandler>
void check(sycl::queue &Queue, sycl::range<D> Extent) {
  std::vector<unsigned> Result(Extent.size() + 1, 0);
  unsigned *Output = sycl::malloc_device<unsigned>(Result.size(), Queue);
  assert(Output);
  Queue.memcpy(Output, Result.data(), Result.size() * sizeof(unsigned)).wait();
  launch<D, UseHandler>(Queue, Output, Extent);
  Queue.memcpy(Result.data(), Output, Result.size() * sizeof(unsigned)).wait();
  sycl::free(Output, Queue);
  for (size_t I = 0; I < Extent.size(); ++I) {
    size_t Remaining = I;
    unsigned Expected = static_cast<unsigned>(I + 1);
    for (int Axis = D - 1; Axis >= 0; --Axis) {
      size_t Coordinate = Remaining % Extent[Axis];
      Remaining /= Extent[Axis];
      Expected += static_cast<unsigned>(Coordinate * (Axis + 7) + Extent[Axis]);
    }
    assert(Result[I] == Expected);
  }
  assert(Result.back() == 0);
}

int main(int Argc, char **Argv) {
  try {
    auto Devices = sycl::device::get_devices(sycl::info::device_type::gpu);
    auto Device = Devices.at(Argc > 1 ? std::stoul(Argv[1]) : 0);
    sycl::queue Queue(Device);
    check<1, false>(Queue, sycl::range<1>{17});
    std::cout << "Small-range AOT verified\n";
    check<1, false>(Queue, sycl::range<1>{65535u * 64u + 65u});
    check<2, true>(Queue, sycl::range<2>{65537, 2});
    check<3, false>(Queue, sycl::range<3>{2, 65536, 3});
    if (Device.get_backend() == sycl::backend::ext_llvm_directx) {
      constexpr size_t Count = (size_t{1} << 32) + 17;
      unsigned Result[6] = {};
      unsigned *Output = sycl::malloc_device<unsigned>(6, Queue);
      assert(Output);
      Queue.memcpy(Output, Result, sizeof(Result)).wait();
      launchWide(Queue, Output, Count);
      Queue.memcpy(Result, Output, sizeof(Result)).wait();
      sycl::free(Output, Queue);
      assert((size_t{Result[1]} << 32 | Result[0]) == Count - 1);
      assert((size_t{Result[3]} << 32 | Result[2]) == Count);
      assert((size_t{Result[5]} << 32 | Result[4]) == Count - 1);
    }
    std::cout << "Large logical ranges verified on "
              << Device.get_info<sycl::info::device::name>() << '\n';
    return 0;
  } catch (const std::exception &Error) {
    std::cerr << Error.what() << '\n';
    return 1;
  }
}
#endif
