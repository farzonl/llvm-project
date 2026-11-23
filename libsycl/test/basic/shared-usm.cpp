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

class SharedIncrement;

sycl::event increment(sycl::queue &Queue, int *Data, unsigned Count,
                      int Value) {
  return Queue.parallel_for<SharedIncrement>(
      sycl::nd_range<1>{sycl::range<1>{(Count + 63u) / 64u * 64u},
                        sycl::range<1>{64}},
      [=](sycl::nd_item<1> Item)
          __attribute__((reqd_work_group_size(64, 1, 1))) {
            unsigned Index = static_cast<unsigned>(Item.get_global_linear_id());
            if (Index < Count)
              Data[Index] += Value;
          });
}

#ifndef __SYCL_DEVICE_ONLY__
#  include <cassert>
#  include <cstdint>
#  include <iostream>
#  include <string>
#  include <vector>

int main(int Argc, char **Argv) {
  try {
    auto Devices = sycl::device::get_devices(sycl::info::device_type::gpu);
    auto Device = Devices.at(Argc > 1 ? std::stoul(Argv[1]) : 0);
    if (!Device.has(sycl::aspect::usm_shared_allocations)) {
      std::cout << "Shared USM is not supported; skipping\n";
      return 0;
    }
    sycl::context Context(Device);
    sycl::queue Queue(Context, Device);
    sycl::queue Other(Context, Device);
    constexpr unsigned Count = 1025;
    int *Data = sycl::aligned_alloc_shared<int>(256, Count + 8, Queue);
    int *Second = sycl::malloc_shared<int>(Count, Queue);
    int *DeviceData = sycl::malloc_device<int>(Count, Queue);
    assert(Data && Second && DeviceData);
    assert(reinterpret_cast<uintptr_t>(Data) % 256 == 0);
    assert(sycl::malloc_shared<int>(0, Queue) == nullptr);
    int *Single = sycl::malloc_shared<int>(1, Queue);
    assert(Single);
    *Single = 41;
    increment(Queue, Single, 1, 1).wait_and_throw();
    assert(*Single == 42);
    sycl::free(Single, Context);

    Data[0] = -77;
    Data[1] = 0;
    Data[2] = 0;
    Data[3] = -66;
    Data[Count + 4] = -99;
    for (unsigned I = 0; I < Count; ++I)
      Data[I + 4] = static_cast<int>(I) - 500;
    if (Device.get_backend() == sycl::backend::ext_llvm_directx) {
      bool Rejected = false;
      try {
        increment(Queue, Data + 1, 1, 3).wait_and_throw();
      } catch (const sycl::exception &Error) {
        if (std::string(Error.what()).find("16-byte aligned") ==
            std::string::npos)
          throw;
        Rejected = true;
      }
      assert(Rejected);
    }

    increment(Queue, Data + 4, Count, 7);
    increment(Queue, Data + 4, Count, 11);
    Queue.wait_and_throw();
    for (unsigned I = 0; I < Count; ++I)
      assert(Data[I + 4] == static_cast<int>(I) - 482);
    Data[516] = 9000;
    increment(Other, Data + 4, Count, -3).wait_and_throw();
    assert(Data[516] == 8997);
    for (unsigned I = 0; I < Count; ++I)
      if (I != 512)
        assert(Data[I + 4] == static_cast<int>(I) - 485);
    assert(Data[0] == -77 && Data[3] == -66 && Data[Count + 4] == -99);

    std::vector<int> Input(Count), Output(Count);
    for (unsigned I = 0; I < Count; ++I)
      Input[I] = 3 * static_cast<int>(I) + 1;
    Queue.memcpy(Second, Input.data(), Count * sizeof(int)).wait_and_throw();
    for (unsigned I = 0; I < Count; ++I)
      assert(Second[I] == Input[I]);
    Queue.memcpy(DeviceData, Second, Count * sizeof(int));
    increment(Queue, DeviceData, Count, 5);
    Queue.memcpy(Second, DeviceData, Count * sizeof(int)).wait_and_throw();
    for (unsigned I = 0; I < Count; ++I)
      assert(Second[I] == Input[I] + 5);
    Queue.memcpy(Data + 4, Second, Count * sizeof(int));
    Queue.fill(Data + 11, 37, 200);
    increment(Queue, Data + 4, Count, 2);
    Queue.memcpy(Output.data(), Data + 4, Count * sizeof(int)).wait_and_throw();
    for (unsigned I = 0; I < Count; ++I) {
      int Expected = I >= 7 && I < 207 ? 39 : Input[I] + 7;
      assert(Output[I] == Expected);
      assert(Data[I + 4] == Expected);
    }
    assert(Data[0] == -77 && Data[3] == -66 && Data[Count + 4] == -99);

    sycl::free(DeviceData, Context);
    sycl::free(Second, Context);
    sycl::free(Data, Context);
    std::cout
        << Device.get_info<sycl::info::device::name>()
        << ": shared USM host/GPU coherence, copies, fill and guards passed\n";
  } catch (const sycl::exception &Error) {
    std::cerr << "Shared USM test failed: " << Error.what() << '\n';
    return 1;
  }
}
#endif
