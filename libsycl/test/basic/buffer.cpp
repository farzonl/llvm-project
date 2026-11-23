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

#ifndef __SYCL_DEVICE_ONLY__
#  include <array>
#  include <atomic>
#  include <cassert>
#  include <iostream>
#  include <optional>
#  include <thread>

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
            << ": buffer dependencies and writeback passed\n";
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
