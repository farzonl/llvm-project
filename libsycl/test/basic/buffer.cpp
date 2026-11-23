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

void initialize(sycl::queue &Queue, sycl::buffer<int> &Buffer) {
  Queue.submit([&](sycl::handler &Handler) {
    auto Output = Buffer.get_access<sycl::access_mode::write>(Handler);
    Handler.single_task<class BufferInitialize>([=] {
      for (unsigned I = 0; I < 8; ++I)
        Output[I] = static_cast<int>(I * 3);
    });
  });
}

void transform(sycl::queue &Queue, sycl::buffer<int> &Input,
               sycl::buffer<float> &Output) {
  Queue.submit([&](sycl::handler &Handler) {
    auto Read = Input.get_access<sycl::access_mode::read>(Handler);
    sycl::accessor Write(Output, Handler);
    Handler.single_task<class BufferTransform>([=] {
      for (unsigned I = 0; I < 8; ++I)
        Write[sycl::id<1>(I)] = static_cast<float>(Read[I]) + 0.5f;
    });
  });
}

void increment(sycl::queue &Queue, sycl::buffer<int> &Buffer) {
  Queue.submit([&](sycl::handler &Handler) {
    auto First = Buffer.get_access(Handler);
    auto Second = First;
    Handler.single_task<class BufferIncrement>([=] {
      for (unsigned I = 0; I < 8; ++I)
        Second[I] = First[I] + 1;
    });
  });
}

template <typename T, unsigned Count> class BufferByteUpdate;

template <typename T, unsigned Count>
void updateBytes(sycl::queue &Queue, sycl::buffer<T> &Buffer) {
  Queue.submit([&](sycl::handler &Handler) {
    auto Access = Buffer.get_access(Handler);
    Handler.parallel_for<BufferByteUpdate<T, Count>>(
        sycl::nd_range<1>{sycl::range<1>{(Count + 63) / 64 * 64},
                          sycl::range<1>{64}},
        [=](sycl::nd_item<1> Item)
            __attribute__((reqd_work_group_size(64, 1, 1))) {
              size_t I = Item.get_global_linear_id();
              if (I < Count) {
                if constexpr (std::is_same_v<T, bool>)
                  Access[I] = !Access[I];
                else if (I % 4 != 3)
                  Access[I] = static_cast<T>(Access[I] + I * 17 + 1);
              }
            });
  });
}

template void updateBytes<bool, 1>(sycl::queue &, sycl::buffer<bool> &);
template void updateBytes<bool, 5>(sycl::queue &, sycl::buffer<bool> &);
template void updateBytes<unsigned char, 1>(sycl::queue &,
                                            sycl::buffer<unsigned char> &);
template void updateBytes<unsigned char, 5>(sycl::queue &,
                                            sycl::buffer<unsigned char> &);
template void updateBytes<unsigned char, 1025>(sycl::queue &,
                                               sycl::buffer<unsigned char> &);
template void updateBytes<signed char, 5>(sycl::queue &,
                                          sycl::buffer<signed char> &);

#ifndef __SYCL_DEVICE_ONLY__
#  include <array>
#  include <atomic>
#  include <cassert>
#  include <iostream>
#  include <optional>
#  include <thread>

template <typename T, unsigned Count>
void checkBytes(sycl::queue &Queue, unsigned Repeats) {
  struct {
    unsigned char Before = 0xAB;
    std::array<T, Count> Values{};
    unsigned char After = 0xCD;
  } Host;
  for (unsigned I = 0; I < Count; ++I) {
    if constexpr (std::is_same_v<T, bool>)
      Host.Values[I] = I % 2 != 0;
    else
      Host.Values[I] = static_cast<T>(I % 2 ? 0x81 : 0xFE);
  }
  const auto Initial = Host.Values;
  {
    sycl::buffer<T> Buffer(Host.Values.data(), sycl::range<1>(Count));
    assert(Buffer.byte_size() == Count);
    for (unsigned I = 0; I < Repeats; ++I)
      updateBytes<T, Count>(Queue, Buffer);
  }
  for (unsigned I = 0; I < Count; ++I) {
    T Expected;
    if constexpr (std::is_same_v<T, bool>)
      Expected = Repeats % 2 ? !Initial[I] : Initial[I];
    else
      Expected = I % 4 == 3
                     ? Initial[I]
                     : static_cast<T>(Initial[I] + Repeats * (I * 17 + 1));
    assert(Host.Values[I] == Expected);
  }
  assert(Host.Before == 0xAB && Host.After == 0xCD);
}

void run(int Argc, char **Argv) {
  auto Devices = sycl::device::get_devices(sycl::info::device_type::gpu);
  auto Device = Devices.at(Argc > 1 ? std::stoul(Argv[1]) : 0);
  std::atomic<bool> AsyncFailed{false};
  const auto OnError = [&](sycl::exception_list Errors) {
    AsyncFailed = true;
    for (const auto &Error : Errors) {
      try {
        std::rethrow_exception(Error);
      } catch (const sycl::exception &Exception) {
        std::cerr << "Asynchronous buffer error: " << Exception.what() << '\n';
      }
    }
  };
  sycl::queue First(Device, OnError);
  sycl::queue Second(First.get_context(), Device, OnError);
  checkBytes<bool, 1>(First, 1);
  checkBytes<bool, 5>(First, 3);
  checkBytes<unsigned char, 1>(First, 2);
  checkBytes<unsigned char, 5>(First, 3);
  checkBytes<unsigned char, 1025>(First, 16);
  checkBytes<signed char, 5>(First, 3);
  std::array<int, 8> Input{};
  std::array<float, 8> Output{};
  for (unsigned I = 0; I < 8; ++I)
    Input[I] = static_cast<int>(I + 9);
  {
    sycl::buffer<int> InputBuffer(Input.data(), sycl::range<1>(8));
    sycl::buffer<float> OutputBuffer(Output.data(), sycl::range<1>(8));
    transform(Second, InputBuffer, OutputBuffer);
  }
  for (unsigned I = 0; I < 8; ++I) {
    assert(Input[I] == static_cast<int>(I + 9));
    assert(Output[I] == static_cast<float>(I) + 9.5f);
  }
  {
    std::array<int, 8> Concurrent{};
    {
      sycl::buffer<int> Buffer(Concurrent.data(), sycl::range<1>(8));
      std::exception_ptr Errors[2];
      auto Worker = [&](sycl::queue &Queue, unsigned Index) {
        try {
          for (unsigned I = 0; I < 4; ++I)
            increment(Queue, Buffer);
        } catch (const sycl::exception &) {
          Errors[Index] = std::current_exception();
        }
      };
      std::thread A(Worker, std::ref(First), 0);
      std::thread B(Worker, std::ref(Second), 1);
      A.join();
      B.join();
      for (const auto &Error : Errors)
        if (Error)
          std::rethrow_exception(Error);
    }
    for (int Value : Concurrent)
      assert(Value == 8);
  }
  {
    sycl::buffer<int> InputBuffer(Input.data(), sycl::range<1>(8));
    sycl::buffer<float> OutputBuffer(Output.data(), sycl::range<1>(8));
    initialize(First, InputBuffer);
    increment(Second, InputBuffer);
    transform(First, InputBuffer, OutputBuffer);
    auto Copy = InputBuffer;
    assert(Copy.size() == 8 && Copy.byte_size() == sizeof(Input));
  }
  for (unsigned I = 0; I < 8; ++I) {
    assert(Input[I] == static_cast<int>(I * 3 + 1));
    assert(Output[I] == static_cast<float>(I * 3) + 1.5f);
  }
  {
    bool Rejected = false;
    sycl::queue Queue(Device, [&](sycl::exception_list Errors) {
      for (const auto &Error : Errors) {
        try {
          std::rethrow_exception(Error);
        } catch (const sycl::exception &Exception) {
          assert(Exception.code() ==
                 sycl::make_error_code(sycl::errc::feature_not_supported));
          Rejected = true;
        }
      }
    });
    std::array<int, 8> Host{};
    Queue.submit([&](sycl::handler &Handler) {
      sycl::buffer<int> Local(Host.data(), sycl::range<1>(8));
      sycl::accessor Access(Local, Handler);
    });
    assert(Rejected);
  }
  {
    std::optional<sycl::accessor<int>> Retained;
    std::array<int, 8> Host{};
    {
      sycl::buffer<int> Buffer(Host.data(), sycl::range<1>(8));
      First.submit(
          [&](sycl::handler &Handler) { Retained.emplace(Buffer, Handler); });
      initialize(First, Buffer);
    }
    for (unsigned I = 0; I < 8; ++I)
      assert(Host[I] == static_cast<int>(I * 3));
    Host[0] = 99;
    Retained.reset();
    assert(Host[0] == 99);
  }
  {
    std::array<int, 8> Migrated{};
    {
      sycl::buffer<int> Buffer(Migrated.data(), sycl::range<1>(8));
      initialize(First, Buffer);
      // A second context cannot consume native events from the first context.
      sycl::queue OtherContext(Device, OnError);
      increment(OtherContext, Buffer);
      increment(First, Buffer);
    }
    for (unsigned I = 0; I < 8; ++I)
      assert(Migrated[I] == static_cast<int>(I * 3 + 2));
  }
  std::cout << Device.get_info<sycl::info::device::name>()
            << ": buffer dependencies, byte storage and writeback passed\n";
  assert(!AsyncFailed.load());
}

int main(int Argc, char **Argv) {
  try {
    run(Argc, Argv);
  } catch (const sycl::exception &Error) {
    std::cerr << "Buffer test failed: " << Error.what() << '\n';
    return 1;
  }
}
#endif
