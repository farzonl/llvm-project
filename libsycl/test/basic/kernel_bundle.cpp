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

class BundleKernel;

void submit(sycl::queue &Queue, const unsigned *Input, unsigned *Output,
            unsigned Increment) {
  Queue.single_task<BundleKernel>([=]() { Output[0] = Input[0] + Increment; });
}

#ifndef __SYCL_DEVICE_ONLY__
#  include <cassert>
#  include <iostream>
#  include <stdexcept>
#  include <unordered_set>

template <typename Callable>
void expectError(sycl::errc Expected, Callable Call) {
  bool Threw = false;
  try {
    Call();
  } catch (const sycl::exception &Error) {
    Threw = true;
    assert(Error.code() == sycl::make_error_code(Expected));
  }
  assert(Threw);
}

int main(int Argc, char **Argv) {
  auto Devices = sycl::device::get_devices(sycl::info::device_type::gpu);
  unsigned Index = Argc > 1 ? std::stoul(Argv[1]) : 0;
  const auto Device = Devices.at(Index);
  const auto Select = [Device](const sycl::device &Candidate) {
    return Candidate == Device ? 1 : -1;
  };
  assert(sycl::platform(Select) == Device.get_platform());
  sycl::queue Queue(Device, sycl::property::queue::in_order{});
  const auto Context = Queue.get_context();
  const auto ID = sycl::get_kernel_id<BundleKernel>();
  auto IDs = sycl::get_kernel_ids();
  assert(std::find(IDs.begin(), IDs.end(), ID) != IDs.end());
  std::unordered_set<sycl::kernel_id> Unique{
      ID, sycl::get_kernel_id<BundleKernel>()};
  assert(Unique.size() == 1);

  const auto Bundle =
      sycl::get_kernel_bundle<BundleKernel, sycl::bundle_state::executable>(
          Context);
  assert(Bundle.has_kernel(ID, Device));
  assert(Bundle.get_kernel_ids().size() == 1);
  assert(Bundle.get_context() == Context);
  assert(Bundle.get_devices() == Context.get_devices());
  const auto Kernel = Bundle.get_kernel(ID);
  assert(Kernel.get_context() == Context);
  assert(Kernel.get_backend() == Queue.get_backend());
  assert(Kernel.get_info<sycl::info::kernel::function_name>() == ID.get_name());
  std::unordered_set<sycl::kernel> Kernels{Kernel, Bundle.get_kernel(ID)};
  assert(Kernels.size() == 1);

  const auto Empty = sycl::get_kernel_bundle<sycl::bundle_state::executable>(
      Context, {Device}, {});
  assert(Empty.empty() && !Empty.has_kernel(ID));
  expectError(sycl::errc::invalid, [&]() { Empty.get_kernel(ID); });
  expectError(sycl::errc::invalid, [&]() {
    sycl::get_kernel_bundle<sycl::bundle_state::executable>(Context, {});
  });
  if (Devices.size() > 1) {
    const auto Other = Devices[(Index + 1) % Devices.size()];
    assert(!Bundle.has_kernel(ID, Other));
    expectError(sycl::errc::invalid, [&]() {
      sycl::get_kernel_bundle<sycl::bundle_state::executable>(Context, {Other});
    });
  }
  expectError(sycl::errc::feature_not_supported, [&]() {
    sycl::get_kernel_bundle<sycl::bundle_state::input>(Context);
  });
  expectError(sycl::errc::feature_not_supported, [&]() {
    sycl::get_kernel_bundle<sycl::bundle_state::object>(Context);
  });

  assert(Device.get_info<sycl::info::device::is_compiler_available>() ==
         Device.has(sycl::aspect::online_compiler));
  assert(Device.get_info<sycl::info::device::is_linker_available>() ==
         Device.has(sycl::aspect::online_linker));
  assert(Device.get_info<sycl::info::device::max_work_group_size>() > 0);
  const auto Version = Device.get_info<sycl::info::device::version>();
  assert(!Version.empty());
  if (Device.get_backend() == sycl::backend::ext_llvm_directx) {
    assert(Version.find("Shader Model 6.") == 0);
    assert(Device.get_info<sycl::info::device::max_work_group_size>() == 1024);
  }

  unsigned Input = 7, Output = 0;
  auto *DeviceInput = sycl::malloc_device<unsigned>(1, Queue);
  auto *DeviceOutput = sycl::malloc_device<unsigned>(1, Queue);
  if (!DeviceInput || !DeviceOutput)
    throw std::runtime_error("device allocation failed");
  Queue.memcpy(DeviceInput, &Input, sizeof(Input));
  submit(Queue, DeviceInput, DeviceOutput, 33);
  Queue.memcpy(&Output, DeviceOutput, sizeof(Output)).wait_and_throw();
  assert(Output == 40);
  sycl::free(DeviceInput, Queue);
  sycl::free(DeviceOutput, Queue);
  std::cout << Device.get_info<sycl::info::device::name>() << " (" << Version
            << "): executable bundle and captured-kernel dispatch passed\n";
}
#endif
