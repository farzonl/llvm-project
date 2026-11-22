//===- OffloadExample.h - Host helpers for liboffload examples ----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef OFFLOAD_EXAMPLES_OFFLOADEXAMPLE_H
#define OFFLOAD_EXAMPLES_OFFLOADEXAMPLE_H

#include <OffloadAPI.h>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace offload_example {

inline void check(ol_result_t Result) {
  if (Result)
    throw std::runtime_error(Result->Details);
}

inline std::string deviceName(ol_device_handle_t Device) {
  size_t Size = 0;
  check(olGetDeviceInfoSize(Device, OL_DEVICE_INFO_PRODUCT_NAME, &Size));
  std::string Name(Size, '\0');
  check(
      olGetDeviceInfo(Device, OL_DEVICE_INFO_PRODUCT_NAME, Size, Name.data()));
  return std::string(Name.c_str());
}

template <typename Signature> struct KernelLauncher;

template <typename... Parameters> struct KernelLauncher<void(Parameters...)> {
  static void launch(ol_queue_handle_t Queue, ol_device_handle_t Device,
                     ol_symbol_handle_t Kernel,
                     const ol_kernel_launch_size_args_t &Launch,
                     Parameters... Values) {
    std::array<void *, sizeof...(Parameters)> Args{&Values...};
    std::array<size_t, sizeof...(Parameters)> Sizes{sizeof(Parameters)...};
    check(olLaunchKernel(Queue, Device, Kernel, &Launch, nullptr, Args.size(),
                         Args.data(), Sizes.data()));
  }
};

// The session owns allocations; buffer views cannot outlive it. Host transfer
// memory must outlive the session or a wait for its queued transfers.
template <typename T> struct DeviceBuffer {
  T *Data;
  size_t Count;
};

class Session {
  int &ExitCode;
  bool Initialized = false;
  ol_device_handle_t Device = nullptr;
  ol_context_handle_t Context = nullptr;
  ol_queue_handle_t Queue = nullptr;
  std::vector<ol_program_handle_t> Programs;
  std::vector<ol_event_handle_t> Events;
  std::vector<void *> Allocations;

public:
  explicit Session(int &ExitCode) : ExitCode(ExitCode) {}
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  ~Session() {
    auto Cleanup = [&](ol_result_t Result) {
      if (Result) {
        std::cerr << "Cleanup failed: " << Result->Details << '\n';
        ExitCode = 1;
      }
    };
    // Drain before releasing allocations and the caller's host transfer memory.
    if (Queue)
      Cleanup(olSyncQueue(Queue));
    for (auto I = Events.rbegin(); I != Events.rend(); ++I)
      if (*I)
        Cleanup(olDestroyEvent(*I));
    if (Queue)
      Cleanup(olDestroyQueue(Queue));
    for (auto I = Programs.rbegin(); I != Programs.rend(); ++I)
      if (*I)
        Cleanup(olDestroyProgram(*I));
    for (auto I = Allocations.rbegin(); I != Allocations.rend(); ++I)
      if (*I)
        Cleanup(olMemFree(Context, *I));
    if (Context)
      Cleanup(olDestroyContext(Context));
    if (Initialized)
      Cleanup(olShutDown());
  }

  std::vector<ol_device_handle_t> initialize(ol_platform_backend_t Backend) {
    if (Initialized)
      throw std::runtime_error("example session is already initialized");
    ol_init_args_t Init = OL_INIT_ARGS_INIT;
    Init.NumPlatforms = 1;
    Init.Platforms = &Backend;
    check(olInit(&Init));
    Initialized = true;
    std::vector<ol_device_handle_t> AllDevices, Devices;
    check(olIterateDevices(
        [](ol_device_handle_t Device, void *Data) {
          static_cast<std::vector<ol_device_handle_t> *>(Data)->push_back(
              Device);
          return true;
        },
        &AllDevices));
    for (auto Candidate : AllDevices) {
      ol_platform_handle_t Platform = nullptr;
      check(olGetDeviceInfo(Candidate, OL_DEVICE_INFO_PLATFORM,
                            sizeof(Platform), &Platform));
      ol_platform_backend_t Kind;
      check(olGetPlatformInfo(Platform, OL_PLATFORM_INFO_BACKEND, sizeof(Kind),
                              &Kind));
      if (Kind == Backend)
        Devices.push_back(Candidate);
    }
    return Devices;
  }

  void select(ol_device_handle_t Selected) {
    if (Context)
      throw std::runtime_error("example session already has a device");
    Device = Selected;
    check(olCreateContext(1, &Device, &Context));
    check(olCreateQueue(Context, Device, &Queue));
  }

  ol_symbol_handle_t loadKernel(const void *Image, size_t Size,
                                const char *Name) {
    Programs.push_back(nullptr);
    check(olCreateProgram(Context, Device, Image, Size, &Programs.back()));
    ol_symbol_handle_t Kernel = nullptr;
    check(olGetSymbol(Programs.back(), Name, OL_SYMBOL_KIND_KERNEL, &Kernel));
    return Kernel;
  }

  template <typename T> DeviceBuffer<T> allocate(size_t Count) {
    if (!Count || Count > std::numeric_limits<size_t>::max() / sizeof(T))
      throw std::runtime_error("invalid example buffer element count");
    Allocations.push_back(nullptr);
    check(olMemAlloc(Context, Device, OL_ALLOC_TYPE_DEVICE, Count * sizeof(T),
                     &Allocations.back()));
    return {static_cast<T *>(Allocations.back()), Count};
  }

  template <typename T> void upload(DeviceBuffer<T> Buffer, const T *Host) {
    check(olMemcpy(Queue, Buffer.Data, Device, Host, nullptr,
                   Buffer.Count * sizeof(T)));
  }

  template <typename T> void download(DeviceBuffer<T> Buffer, T *Host) {
    check(olMemcpy(Queue, Host, nullptr, Buffer.Data, Device,
                   Buffer.Count * sizeof(T)));
  }

  template <typename Signature, typename... Parameters>
  void launch(ol_symbol_handle_t Kernel,
              const ol_kernel_launch_size_args_t &Launch,
              Parameters... Values) {
    KernelLauncher<Signature>::launch(Queue, Device, Kernel, Launch, Values...);
  }

  ol_event_handle_t record(bool Profiling = false) {
    Events.push_back(nullptr);
    check(olCreateEvent(Queue,
                        Profiling ? OL_EVENT_FLAGS_ENABLE_PROFILING
                                  : OL_EVENT_FLAGS_NONE,
                        &Events.back()));
    return Events.back();
  }

  void wait(ol_event_handle_t Event) { check(olSyncEvent(Event)); }

  float elapsed(ol_event_handle_t Start, ol_event_handle_t Stop) {
    wait(Start);
    wait(Stop);
    float Milliseconds = 0;
    check(olGetEventElapsedTime(Start, Stop, &Milliseconds));
    if (!std::isfinite(Milliseconds) || Milliseconds < 0)
      throw std::runtime_error("invalid GPU event timing");
    return Milliseconds;
  }
};

} // namespace offload_example

#endif
