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

#include <OffloadAPI.h>

#include <charconv>
#include <cmath>
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

void check(ol_result_t Result) {
  if (Result)
    throw std::runtime_error(Result->Details);
}

template <typename Signature> struct KernelLauncher;

template <typename... Parameters> struct KernelLauncher<void(Parameters...)> {
  static void launch(ol_queue_handle_t Queue, ol_device_handle_t Device,
                     ol_symbol_handle_t Kernel,
                     const ol_kernel_launch_size_args_t &Launch,
                     Parameters... Values) {
    void *Args[] = {&Values...};
    size_t Sizes[] = {sizeof(Parameters)...};
    check(olLaunchKernel(Queue, Device, Kernel, &Launch, nullptr,
                         sizeof...(Parameters), Args, Sizes));
  }
};

size_t number(std::string_view Text) {
  size_t Value = 0;
  auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value);
  if (Result.ec != std::errc{} || Result.ptr != Text.data() + Text.size())
    throw std::runtime_error("expected a nonnegative decimal integer");
  return Value;
}

struct Resources {
  int &ExitCode;
  bool Initialized = false;
  std::vector<float> A, B, C;
  ol_context_handle_t Context = nullptr;
  ol_queue_handle_t Queue = nullptr;
  ol_program_handle_t Program = nullptr;
  ol_event_handle_t Start = nullptr, Stop = nullptr, Readback = nullptr;
  void *DeviceA = nullptr, *DeviceB = nullptr, *DeviceC = nullptr;

  explicit Resources(int &ExitCode) : ExitCode(ExitCode) {}
  Resources(const Resources &) = delete;
  Resources &operator=(const Resources &) = delete;

  ~Resources() {
    auto Cleanup = [&](ol_result_t Result) {
      if (Result) {
        std::cerr << "Cleanup failed: " << Result->Details << '\n';
        ExitCode = 1;
      }
    };
    // Drain before releasing either device allocations or host readback memory.
    if (Queue)
      Cleanup(olSyncQueue(Queue));
    for (auto Event : {Readback, Stop, Start})
      if (Event)
        Cleanup(olDestroyEvent(Event));
    if (Queue)
      Cleanup(olDestroyQueue(Queue));
    if (Program)
      Cleanup(olDestroyProgram(Program));
    for (auto Pointer : {DeviceC, DeviceB, DeviceA})
      if (Pointer)
        Cleanup(olMemFree(Context, Pointer));
    if (Context)
      Cleanup(olDestroyContext(Context));
    if (Initialized)
      Cleanup(olShutDown());
  }
};

std::string deviceName(ol_device_handle_t Device) {
  size_t Size = 0;
  check(olGetDeviceInfoSize(Device, OL_DEVICE_INFO_PRODUCT_NAME, &Size));
  std::string Name(Size, '\0');
  check(
      olGetDeviceInfo(Device, OL_DEVICE_INFO_PRODUCT_NAME, Size, Name.data()));
  return std::string(Name.c_str());
}

void run(int Argc, char **Argv, Resources &R) {
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

  ol_platform_backend_t Backend = OL_PLATFORM_BACKEND_DIRECTX;
  ol_init_args_t Init = OL_INIT_ARGS_INIT;
  Init.NumPlatforms = 1;
  Init.Platforms = &Backend;
  check(olInit(&Init));
  R.Initialized = true;
  std::vector<ol_device_handle_t> AllDevices, Devices;
  check(olIterateDevices(
      [](ol_device_handle_t Device, void *Data) {
        static_cast<std::vector<ol_device_handle_t> *>(Data)->push_back(Device);
        return true;
      },
      &AllDevices));
  for (auto Device : AllDevices) {
    ol_platform_handle_t Platform = nullptr;
    check(olGetDeviceInfo(Device, OL_DEVICE_INFO_PLATFORM, sizeof(Platform),
                          &Platform));
    ol_platform_backend_t Candidate;
    check(olGetPlatformInfo(Platform, OL_PLATFORM_INFO_BACKEND,
                            sizeof(Candidate), &Candidate));
    if (Candidate == OL_PLATFORM_BACKEND_DIRECTX)
      Devices.push_back(Device);
  }
  if (Devices.empty())
    throw std::runtime_error("no DirectX device is available");
  if (List) {
    for (size_t I = 0; I < Devices.size(); ++I)
      std::cout << I << ": " << deviceName(Devices[I]) << '\n';
    return;
  }
  if (Index >= Devices.size())
    throw std::runtime_error("device index is out of range; use --list");
  auto Device = Devices[Index];
  std::cout << "Device " << Index << ": " << deviceName(Device) << '\n';

  check(olCreateContext(1, &Device, &R.Context));
  check(olCreateQueue(R.Context, Device, &R.Queue));
  check(olCreateProgram(R.Context, Device, DeviceImage, sizeof(DeviceImage),
                        &R.Program));
  ol_symbol_handle_t Kernel = nullptr;
  check(olGetSymbol(R.Program, "vector_add", OL_SYMBOL_KIND_KERNEL, &Kernel));

  R.A.resize(Count);
  R.B.resize(Count);
  R.C.assign(Count, std::numeric_limits<float>::quiet_NaN());
  for (size_t I = 0; I < Count; ++I) {
    R.A[I] = static_cast<float>(I % 1024) * 0.25f;
    R.B[I] = static_cast<float>(I % 127) * 0.5f;
  }
  size_t Bytes = Count * sizeof(float);
  for (auto *Pointer : {&R.DeviceA, &R.DeviceB, &R.DeviceC})
    check(olMemAlloc(R.Context, Device, OL_ALLOC_TYPE_DEVICE, Bytes, Pointer));
  check(olMemcpy(R.Queue, R.DeviceA, Device, R.A.data(), nullptr, Bytes));
  check(olMemcpy(R.Queue, R.DeviceB, Device, R.B.data(), nullptr, Bytes));
  check(olCreateEvent(R.Queue, OL_EVENT_FLAGS_ENABLE_PROFILING, &R.Start));

  ol_kernel_launch_size_args_t Launch{};
  Launch.Dimensions = 1;
  Launch.NumGroups = {
      static_cast<uint32_t>((Count + GroupSize - 1) / GroupSize), 1, 1};
  Launch.GroupSize = {GroupSize, 1, 1};
  uint32_t KernelCount = static_cast<uint32_t>(Count);
  KernelLauncher<decltype(vector_add)>::launch(
      R.Queue, Device, Kernel, Launch, static_cast<const float *>(R.DeviceA),
      static_cast<const float *>(R.DeviceB), static_cast<float *>(R.DeviceC),
      KernelCount);
  check(olCreateEvent(R.Queue, OL_EVENT_FLAGS_ENABLE_PROFILING, &R.Stop));
  check(olMemcpy(R.Queue, R.C.data(), nullptr, R.DeviceC, Device, Bytes));
  check(olCreateEvent(R.Queue, OL_EVENT_FLAGS_NONE, &R.Readback));
  check(olSyncEvent(R.Readback));
  check(olSyncEvent(R.Start));
  check(olSyncEvent(R.Stop));
  float Milliseconds = 0;
  check(olGetEventElapsedTime(R.Start, R.Stop, &Milliseconds));
  if (!std::isfinite(Milliseconds) || Milliseconds < 0)
    throw std::runtime_error("invalid GPU event timing");
  for (size_t I = 0; I < Count; ++I) {
    float Expected = R.A[I] + R.B[I];
    if (R.C[I] != Expected)
      throw std::runtime_error("verification failed at element " +
                               std::to_string(I) + ": expected " +
                               std::to_string(Expected) + ", got " +
                               std::to_string(R.C[I]));
  }
  std::cout << "PASS: " << Count << " elements verified; GPU dispatch "
            << Milliseconds << " ms (excludes upload/readback)\n";
}

} // namespace

int main(int Argc, char **Argv) {
  int ExitCode = 1;
  {
    Resources R(ExitCode);
    try {
      run(Argc, Argv, R);
      ExitCode = 0;
    } catch (const std::exception &Error) {
      std::cerr << "Error: " << Error.what() << '\n';
    }
  }
  return ExitCode;
}

#endif
