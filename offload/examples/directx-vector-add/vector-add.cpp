//===- vector-add.cpp - Single-source liboffload DirectX demo
//------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifdef __DIRECTX__
#include <gpuintrin.h>
#define DIRECTX_KERNEL                                                         \
  __gpu_kernel __attribute__((reqd_work_group_size(64, 1, 1)))
#else
#define DIRECTX_KERNEL
#endif

extern "C" DIRECTX_KERNEL void vector_add(const float *A, const float *B,
                                          float *C, unsigned Count)
#ifdef __DIRECTX__
{
  unsigned I = __gpu_block_id(0) * __gpu_num_threads(0) + __gpu_thread_id(0);
  if (I < Count)
    C[I] = A[I] + B[I];
}
#else
    ;

#include "../OffloadExample.h"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

const unsigned char DeviceImage[] = {
#embed "vector-add.bin"
};

size_t number(std::string_view Text) {
  size_t Value = 0;
  auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
  if (Result.ec != std::errc{} || Result.ptr != Text.data() + Text.size())
    throw std::runtime_error("expected a nonnegative decimal integer");
  return Value;
}

void run(int Argc, char **Argv, int &ExitCode) {
  bool List = Argc == 2 && std::string_view(Argv[1]) == "--list";
  if (!List && Argc > 3)
    throw std::runtime_error(
        "Usage: directx-vector-add --list\n"
        "       directx-vector-add [device-index] [elements]");
  size_t Index = !List && Argc > 1 ? number(Argv[1]) : 0;
  size_t Count = Argc > 2 ? number(Argv[2]) : 1048589;
  constexpr size_t GroupSize = 64;
  if (!Count || Count > GroupSize * 65535)
    throw std::runtime_error("elements must be between 1 and 4194240");

  std::vector<float> A, B, C;
  offload_example::Session Session(ExitCode);
  auto Devices = Session.initialize(OL_PLATFORM_BACKEND_DIRECTX);
  if (Devices.empty())
    throw std::runtime_error("no DirectX device is available");
  if (List) {
    for (size_t I = 0; I < Devices.size(); ++I)
      std::cout << I << ": " << offload_example::deviceName(Devices[I]) << '\n';
    ExitCode = 0;
    return;
  }
  if (Index >= Devices.size())
    throw std::runtime_error("device index is out of range; use --list");
  auto Device = Devices[Index];
  std::cout << "Device " << Index << ": " << offload_example::deviceName(Device)
            << '\n';
  Session.select(Device);
  auto Kernel =
      Session.loadKernel(DeviceImage, sizeof(DeviceImage), "vector_add");

  A.resize(Count);
  B.resize(Count);
  C.assign(Count, std::numeric_limits<float>::quiet_NaN());
  for (size_t I = 0; I < Count; ++I) {
    A[I] = static_cast<float>(I % 1024) * 0.25f;
    B[I] = static_cast<float>(I % 127) * 0.5f;
  }
  auto DeviceA = Session.allocate<float>(Count);
  auto DeviceB = Session.allocate<float>(Count);
  auto DeviceC = Session.allocate<float>(Count);
  Session.upload(DeviceA, A.data());
  Session.upload(DeviceB, B.data());
  auto Start = Session.record(true);

  ol_kernel_launch_size_args_t Launch{};
  Launch.Dimensions = 1;
  Launch.NumGroups = {
      static_cast<uint32_t>((Count + GroupSize - 1) / GroupSize), 1, 1};
  Launch.GroupSize = {GroupSize, 1, 1};
  uint32_t KernelCount = static_cast<uint32_t>(Count);
  Session.launch<decltype(vector_add)>(Kernel, Launch, DeviceA.Data,
                                       DeviceB.Data, DeviceC.Data, KernelCount);
  auto Stop = Session.record(true);
  Session.download(DeviceC, C.data());
  Session.wait(Session.record());
  float Milliseconds = Session.elapsed(Start, Stop);
  for (size_t I = 0; I < Count; ++I) {
    float Expected = A[I] + B[I];
    if (C[I] != Expected)
      throw std::runtime_error("verification failed at element " +
                               std::to_string(I) + ": expected " +
                               std::to_string(Expected) + ", got " +
                               std::to_string(C[I]));
  }
  std::cout << "PASS: " << Count << " elements verified; GPU dispatch "
            << Milliseconds << " ms (excludes upload/readback)\n";
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
