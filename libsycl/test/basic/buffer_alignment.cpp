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

template <std::size_t Alignment> struct alignas(Alignment) AlignedPacket {
  using value_type = int;
  int Values[Alignment / sizeof(int)];
  static constexpr std::size_t size() { return Alignment / sizeof(int); }
  int &operator[](std::size_t I) { return Values[I]; }
  const int &operator[](std::size_t I) const { return Values[I]; }
};

using IntVector = sycl::vec<int, 4>;
using FloatVector = sycl::vec<float, 16>;
using Packet = AlignedPacket<256>;

template <typename T> class AlignedBufferKernel;

template <typename T>
void updateElements(sycl::queue &Queue, sycl::buffer<T, 2> &Buffer, int Bias) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::accessor Access(Buffer, Handler);
    auto Columns = Buffer.get_range()[1];
    Handler.parallel_for<AlignedBufferKernel<T>>(
        sycl::range<1>(Buffer.size() * T::size()), [=](sycl::item<1> Item) {
          auto Object = Item.get_linear_id() / T::size();
          auto Component = Item.get_linear_id() % T::size();
          Access[sycl::id<2>(Object / Columns, Object % Columns)][Component] +=
              static_cast<typename T::value_type>(
                  Bias + 3 * static_cast<int>(Component) + 7);
        });
  });
}

template void updateElements<IntVector>(sycl::queue &,
                                        sycl::buffer<IntVector, 2> &, int);
template void updateElements<FloatVector>(sycl::queue &,
                                          sycl::buffer<FloatVector, 2> &, int);
template void updateElements<Packet>(sycl::queue &, sycl::buffer<Packet, 2> &,
                                     int);

#ifndef __SYCL_DEVICE_ONLY__
#  include <array>
#  include <cassert>
#  include <cstdint>
#  include <iostream>
#  include <limits>
#  include <optional>

void checkStorageErrors() {
  const auto Reject = [](std::size_t Bytes, std::size_t Alignment,
                         sycl::errc Expected) {
    bool Rejected = false;
    try {
      auto *Buffer = sycl::detail::makeBuffer(nullptr, Bytes, Alignment);
      sycl::detail::releaseBufferOwner(Buffer);
    } catch (const sycl::exception &Error) {
      Rejected = Error.code() == sycl::make_error_code(Expected);
    }
    assert(Rejected);
  };
  Reject(1, 3, sycl::errc::invalid);
  Reject(std::numeric_limits<std::size_t>::max(), 2,
         sycl::errc::memory_allocation);
}

template <typename T> void initialize(T *Data, std::size_t Count) {
  for (std::size_t I = 0; I < Count; ++I)
    for (std::size_t C = 0; C < T::size(); ++C)
      Data[I][C] = static_cast<typename T::value_type>(I * 100 + C);
}

template <typename T>
void updateExpected(T *Data, std::size_t Count, int Bias) {
  for (std::size_t I = 0; I < Count; ++I)
    for (std::size_t C = 0; C < T::size(); ++C)
      Data[I][C] += static_cast<typename T::value_type>(
          Bias + 3 * static_cast<int>(C) + 7);
}

template <typename T>
void verify(const T *Data, const T *Expected, std::size_t Count) {
  assert(reinterpret_cast<std::uintptr_t>(Data) % alignof(T) == 0);
  for (std::size_t I = 0; I < Count; ++I)
    for (std::size_t C = 0; C < T::size(); ++C)
      assert(Data[I][C] == Expected[I][C]);
}

template <typename T>
void checkBuffers(sycl::queue &First, sycl::queue &Second) {
  std::array<T, 15> Expected{};
  initialize(Expected.data(), Expected.size());
  {
    sycl::buffer<T, 2> Owned(sycl::range<2>(3, 5));
    {
      sycl::host_accessor Host(Owned, sycl::write_only);
      assert(reinterpret_cast<std::uintptr_t>(Host.get_pointer()) %
                 alignof(T) ==
             0);
      initialize(Host.get_pointer(), Owned.size());
    }
    updateElements(First, Owned, 1);
    updateElements(Second, Owned, 5);
    updateExpected(Expected.data(), Expected.size(), 1);
    updateExpected(Expected.data(), Expected.size(), 5);
    {
      sycl::host_accessor Host(Owned);
      verify(Host.get_pointer(), Expected.data(), Expected.size());
      Host[2][4][T::size() - 1] = 777;
      Expected.back()[T::size() - 1] = 777;
    }
    updateElements(First, Owned, 9);
    updateExpected(Expected.data(), Expected.size(), 9);
    sycl::host_accessor Read(Owned, sycl::read_only);
    verify(Read.get_pointer(), Expected.data(), Expected.size());
  }
  {
    std::array<T, 11> Host{};
    initialize(Host.data(), Host.size());
    auto Expected = Host;
    updateExpected(Expected.data() + 1, 9, 3);
    {
      sycl::buffer<T, 2> Caller(Host.data() + 1, sycl::range<2>(3, 3));
      Host.front()[0] = Expected.front()[0] = -91;
      Host.back()[0] = Expected.back()[0] = -93;
      updateElements(First, Caller, 3);
      sycl::host_accessor Read(Caller, sycl::read_only);
      verify(Read.get_pointer(), Expected.data() + 1, 9);
    }
    verify(Host.data(), Expected.data(), Host.size());
  }
  {
    std::array<T, 2> Host{};
    std::array<T, 2> Expected{};
    initialize(Expected.data(), Expected.size());
    std::optional<sycl::host_accessor<T, 2>> Retained;
    {
      sycl::buffer<T, 2> Buffer(Host.data(), sycl::range<2>(1, 2));
      Retained.emplace(Buffer);
      initialize(Retained->get_pointer(), Host.size());
    }
    verify(Retained->get_pointer(), Expected.data(), Expected.size());
    Retained.reset();
    verify(Host.data(), Expected.data(), Expected.size());
  }
}

int main(int Argc, char **Argv) {
  try {
    checkStorageErrors();
    auto Devices = sycl::device::get_devices(sycl::info::device_type::gpu);
    auto Device = Devices.at(Argc > 1 ? std::stoul(Argv[1]) : 0);
    sycl::queue First(Device);
    sycl::context OtherContext(Device);
    sycl::queue Second(OtherContext, Device);
    checkBuffers<IntVector>(First, Second);
    checkBuffers<FloatVector>(First, Second);
    checkBuffers<Packet>(First, Second);
    First.wait_and_throw();
    Second.wait_and_throw();
    std::cout << Device.get_info<sycl::info::device::name>()
              << ": 16/64/256-byte aligned buffers, host access and writeback "
                 "passed\n";
    return 0;
  } catch (const sycl::exception &Error) {
    std::cerr << "aligned buffer test failed: " << Error.what() << '\n';
    return 1;
  }
}
#endif
