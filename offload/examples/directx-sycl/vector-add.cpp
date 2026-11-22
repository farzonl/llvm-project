//===- vector-add.cpp - Experimental SYCL DirectX parallel demo
//---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <sycl/sycl.hpp>

void submitVectorAdd(sycl::queue &Queue, const float *A, const float *B,
                     float *C, unsigned Count) {
  size_t Global = (size_t(Count) + 63) / 64 * 64;
  Queue.parallel_for<class DirectXVectorAdd>(
      sycl::nd_range<1>{sycl::range<1>{Global}, sycl::range<1>{64}},
      [=](sycl::nd_item<1> Item)
          __attribute__((reqd_work_group_size(64, 1, 1))) {
            size_t I = Item.get_global_linear_id();
            if (I < Count)
              C[I] = A[I] + B[I];
          });
}

#ifndef __SYCL_DEVICE_ONLY__
#include <charconv>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

size_t number(std::string_view Text) {
  size_t Value = 0;
  auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
  if (Result.ec != std::errc{} || Result.ptr != Text.data() + Text.size())
    throw std::runtime_error("expected a nonnegative decimal integer");
  return Value;
}

// Host arrays precede this owner so its queue drains before they are destroyed.
struct Resources {
  sycl::queue Queue;
  int &ExitCode;
  float *A = nullptr, *B = nullptr, *C = nullptr;

  Resources(const sycl::device &Device, int &ExitCode)
      : Queue(Device, sycl::property::queue::in_order{}), ExitCode(ExitCode) {}
  Resources(const Resources &) = delete;
  Resources &operator=(const Resources &) = delete;
  ~Resources() {
    auto Cleanup = [&](auto Action) {
      try {
        Action();
      } catch (const sycl::exception &Error) {
        std::cerr << "Cleanup failed: " << Error.what() << '\n';
        ExitCode = 1;
      }
    };
    Cleanup([&] { Queue.wait_and_throw(); });
    for (auto Pointer : {C, B, A})
      if (Pointer)
        Cleanup([&] { sycl::free(Pointer, Queue); });
  }
};

void run(int Argc, char **Argv, int &ExitCode) {
  bool List = Argc == 2 && std::string_view(Argv[1]) == "--list";
  if (!List && Argc > 3)
    throw std::runtime_error(
        "Usage: directx-sycl-vector-add --list\n"
        "       directx-sycl-vector-add [device-index] [elements]");
  size_t Index = !List && Argc > 1 ? number(Argv[1]) : 0;
  size_t Count = Argc > 2 ? number(Argv[2]) : 65537;
  if (!Count || Count > 65537)
    throw std::runtime_error("elements must be between 1 and 65537");
  std::vector<sycl::device> Devices;
  for (auto Device : sycl::device::get_devices())
    if (Device.get_backend() == sycl::backend::ext_llvm_directx)
      Devices.push_back(Device);
  if (Devices.empty())
    throw std::runtime_error("no DirectX SYCL device is available");
  if (List) {
    for (size_t I = 0; I < Devices.size(); ++I)
      std::cout << I << ": " << Devices[I].get_info<sycl::info::device::name>()
                << '\n';
    ExitCode = 0;
    return;
  }
  if (Index >= Devices.size())
    throw std::runtime_error("device index is out of range; use --list");
  auto Device = Devices[Index];
  std::cout << "Device " << Index << ": "
            << Device.get_info<sycl::info::device::name>() << '\n';
  if (!Device.has(sycl::aspect::usm_device_allocations))
    throw std::runtime_error("device USM allocations are not supported");
  std::vector<float> A(Count), B(Count);
  std::vector<float> C(Count, std::numeric_limits<float>::quiet_NaN());
  for (size_t I = 0; I < Count; ++I) {
    A[I] = static_cast<float>(I % 1024) * 0.25f;
    B[I] = static_cast<float>(I % 127) * 0.5f;
  }
  Resources R(Device, ExitCode);
  R.A = sycl::malloc_device<float>(Count, R.Queue);
  R.B = sycl::malloc_device<float>(Count, R.Queue);
  R.C = sycl::malloc_device<float>(Count, R.Queue);
  if (!R.A || !R.B || !R.C)
    throw std::runtime_error("device USM allocation failed");
  size_t Bytes = Count * sizeof(float);
  R.Queue.memcpy(R.A, A.data(), Bytes);
  R.Queue.memcpy(R.B, B.data(), Bytes);
  submitVectorAdd(R.Queue, R.A, R.B, R.C, static_cast<unsigned>(Count));
  R.Queue.memcpy(C.data(), R.C, Bytes).wait_and_throw();
  for (size_t I = 0; I < Count; ++I)
    if (C[I] != A[I] + B[I])
      throw std::runtime_error("verification failed at element " +
                               std::to_string(I));
  std::cout << "PASS: " << Count
            << " elements verified through sycl::queue::parallel_for\n";
  ExitCode = 0;
}

} // namespace

int main(int Argc, char **Argv) {
  int ExitCode = 1;
  try {
    run(Argc, Argv, ExitCode);
  } catch (const std::exception &Error) {
    std::cerr << "Error: " << Error.what() << '\n';
  }
  return ExitCode;
}
#endif
