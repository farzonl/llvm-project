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

struct alignas(128) Aligned {
  int Value[4];
};
template <int D> class LocalStorageKernel;

template <int D>
void launchLocal(sycl::queue &Queue, sycl::buffer<int> &Result,
                 sycl::nd_range<D> Range, sycl::range<D> Storage) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::local_accessor<int, D> Local(Storage, Handler);
    auto Alias = Local;
    sycl::local_accessor<Aligned> Other(sycl::range<1>(Storage.size()),
                                        Handler);
    sycl::local_accessor<float> Floats(sycl::range<1>(Storage.size() + 5),
                                       Handler);
    auto Output = Result.get_access(Handler);
    Handler.parallel_for<class LocalStorageKernel<D>>(
        Range, [=](sycl::nd_item<D> Item) {
          auto I = Item.get_local_linear_id();
          auto Count = Item.get_local_range().size();
          int Value = static_cast<int>(Item.get_group_linear_id() * 100 + I);
          auto ID = Item.get_local_id();
          if constexpr (D == 1)
            Local[ID] = Value;
          else if constexpr (D == 2)
            Local[ID[0]][ID[1]] = Value;
          else
            Local[ID[0]][ID[1]][ID[2]] = Value;
          auto Pointer =
              Other.template get_multi_ptr<sycl::access::decorated::yes>();
          Pointer[I].Value[0] = Value * 3;
          Floats[I] = static_cast<float>(Value * 2) + 0.5f;
          auto Raw =
              Alias.template get_multi_ptr<sycl::access::decorated::no>();
          if (I == 0)
            Raw[Storage.size() - 1] = 1234;
          sycl::group_barrier(Item.get_group());
          Output[Item.get_global_linear_id()] =
              Raw[(I + 1) % Count] + Pointer[(I + 1) % Count].Value[0] +
              Raw[Storage.size() - 1] +
              static_cast<int>(Floats[(I + 1) % Count]);
        });
  });
}

template void launchLocal<1>(sycl::queue &, sycl::buffer<int> &,
                             sycl::nd_range<1>, sycl::range<1>);
template void launchLocal<2>(sycl::queue &, sycl::buffer<int> &,
                             sycl::nd_range<2>, sycl::range<2>);
template void launchLocal<3>(sycl::queue &, sycl::buffer<int> &,
                             sycl::nd_range<3>, sycl::range<3>);

void launchFixed(sycl::queue &Queue, sycl::buffer<int> &Result, size_t Count) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::local_accessor<int> Local(sycl::range<1>(Count), Handler);
    auto Alias = Local;
    auto Output = Result.get_access(Handler);
    Handler.parallel_for<class FixedLocalKernel>(
        sycl::nd_range<1>(64, 32),
        [=](sycl::nd_item<1> Item)
            __attribute__((reqd_work_group_size(32, 1, 1))) {
              auto I = Item.get_local_linear_id();
              Local[I] = static_cast<int>(Item.get_global_linear_id());
              sycl::group_barrier(Item.get_group());
              Output[Item.get_global_linear_id()] = Alias[31 - I];
            });
  });
}

#ifndef __SYCL_DEVICE_ONLY__
#  include <array>
#  include <cassert>
#  include <cstdlib>
#  include <iostream>
#  include <limits>
#  include <string>
#  include <vector>

template <int D>
void checkLocal(sycl::queue &Queue, sycl::range<D> Shape,
                sycl::range<D> Storage, unsigned Groups = 3) {
  auto Global = Shape;
  Global[0] *= Groups;
  std::vector<int> Result(Global.size(), -1);
  {
    sycl::buffer<int> Buffer(Result.data(), sycl::range<1>(Result.size()));
    launchLocal(Queue, Buffer, sycl::nd_range<D>(Global, Shape), Storage);
  }
  for (size_t I = 0; I < Result.size(); ++I)
    assert(Result[I] == 6 * static_cast<int>((I / Shape.size()) * 100 +
                                             (I + 1) % Shape.size()) +
                            1234);
}

void checkFixed(sycl::queue &Queue, size_t Count) {
  std::array<int, 64> Result{};
  {
    sycl::buffer<int> Buffer(Result.data(), sycl::range<1>(Result.size()));
    launchFixed(Queue, Buffer, Count);
  }
  for (size_t I = 0; I < Result.size(); ++I)
    assert(Result[I] == static_cast<int>((I / 32) * 32 + 31 - I % 32));
}
int main(int Argc, char **Argv) {
  try {
    auto Devices = sycl::device::get_devices(sycl::info::device_type::gpu);
    auto Device = Devices.at(Argc > 1 ? std::stoul(Argv[1]) : 0);
    sycl::queue Queue(Device);
    checkLocal(Queue, sycl::range<1>(8), sycl::range<1>(11));
    checkLocal(Queue, sycl::range<1>(8), sycl::range<1>(19));
    checkLocal(Queue, sycl::range<1>(5), sycl::range<1>(19));
    checkLocal(Queue, sycl::range<2>(2, 3), sycl::range<2>(5, 3));
    checkLocal(Queue, sycl::range<3>(2, 3, 2), sycl::range<3>(4, 3, 2));
    checkLocal(Queue, sycl::range<1>(8), sycl::range<1>(11), 7);
    checkLocal(Queue, sycl::range<1>(8), sycl::range<1>(240));
    checkFixed(Queue, 33);
    checkFixed(Queue, 48);
    checkFixed(Queue, 8192);
#  ifdef _WIN32
    char *CompilerPath = nullptr;
    size_t PathSize = 0;
    assert(_dupenv_s(&CompilerPath, &PathSize,
                     "OFFLOAD_DIRECTX_COMPILER_PATH") == 0);
    std::string SavedPath = CompilerPath ? CompilerPath : "";
    std::free(CompilerPath);
    assert(_putenv_s("OFFLOAD_DIRECTX_COMPILER_PATH",
                     "nonexistent-local-accessor-compiler") == 0);
    checkLocal(Queue, sycl::range<1>(8), sycl::range<1>(11), 9);
    checkFixed(Queue, 48);
    bool ColdRejected = false;
    try {
      checkFixed(Queue, 55);
    } catch (const sycl::exception &Error) {
      ColdRejected = std::string(Error.what()).find("specialization tool") !=
                     std::string::npos;
    }
    assert(_putenv_s("OFFLOAD_DIRECTX_COMPILER_PATH", SavedPath.c_str()) == 0);
    assert(ColdRejected);
#  endif
    bool Rejected = false;
    try {
      Queue.submit([&](sycl::handler &Handler) {
        sycl::local_accessor<int> Invalid(sycl::range<1>(8193), Handler);
      });
    } catch (const sycl::exception &Error) {
      Rejected = Error.code() == sycl::errc::invalid;
    }
    assert(Rejected);
    for (size_t Size : {size_t(0), std::numeric_limits<size_t>::max()}) {
      Rejected = false;
      try {
        Queue.submit([&](sycl::handler &Handler) {
          sycl::local_accessor<int, 2> Invalid(sycl::range<2>(2, Size),
                                               Handler);
        });
      } catch (const sycl::exception &Error) {
        Rejected = Error.code() == sycl::errc::invalid;
      }
      assert(Rejected);
    }
    Rejected = false;
    try {
      Queue.submit([&](sycl::handler &Handler) {
        sycl::local_accessor<int> Local(sycl::range<1>(8), Handler);
        Handler.single_task<class InvalidLocalSingleTask>(
            [=]() { (void)Local; });
      });
    } catch (const sycl::exception &Error) {
      Rejected = Error.code() == sycl::errc::invalid;
    }
    assert(Rejected);
    Rejected = false;
    try {
      checkLocal(Queue, sycl::range<1>(8), sycl::range<1>(241));
    } catch (const sycl::exception &Error) {
      Rejected = std::string(Error.what()).find("exceed 32768 bytes") !=
                 std::string::npos;
    }
    assert(Rejected);
    std::cout
        << Device.get_info<sycl::info::device::name>()
        << ": local allocation, aliasing, alignment and barriers passed\n";
    return 0;
  } catch (const sycl::exception &Error) {
    std::cerr << "local accessor test failed: " << Error.what() << '\n';
    return 1;
  }
}
#endif
