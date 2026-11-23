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

void submitIntegerFunctionObjects(sycl::queue &Queue, const unsigned *Input,
                                  unsigned *Output) {
  Queue.single_task<class IntegerFunctionObjects>([=]() {
    unsigned X = Input[0], Y = Input[1];
    Output[0] = sycl::plus<unsigned>{}(X, Y);
    Output[1] = sycl::plus<>{}(X, Y);
    Output[2] = sycl::multiplies<unsigned>{}(X, Y);
    Output[3] = sycl::multiplies<>{}(X, Y);
    Output[4] = sycl::bit_and<unsigned>{}(X, Y);
    Output[5] = sycl::bit_and<>{}(X, Y);
    Output[6] = sycl::bit_or<unsigned>{}(X, Y);
    Output[7] = sycl::bit_or<>{}(X, Y);
    Output[8] = sycl::bit_xor<unsigned>{}(X, Y);
    Output[9] = sycl::bit_xor<>{}(X, Y);
    Output[10] = sycl::logical_and<unsigned>{}(X, Y);
    Output[11] = sycl::logical_and<>{}(X, Y);
    Output[12] = sycl::logical_or<unsigned>{}(X, Y);
    Output[13] = sycl::logical_or<>{}(X, Y);
    Output[14] = sycl::minimum<unsigned>{}(X, Y);
    Output[15] = sycl::minimum<>{}(X, Y);
    Output[16] = sycl::maximum<unsigned>{}(X, Y);
    Output[17] = sycl::maximum<>{}(X, Y);
  });
}

void submitFloatingPointFunctionObjects(sycl::queue &Queue, const float *Input,
                                        float *Output) {
  Queue.single_task<class FloatingPointFunctionObjects>([=]() {
    float X = Input[0], Y = Input[1];
    Output[0] = sycl::minimum<float>{}(X, Y);
    Output[1] = sycl::maximum<>{}(X, Y);
    Output[2] = sycl::minimum<>{}(Y, X);
    Output[3] = sycl::maximum<float>{}(Y, X);
    Output[4] = sycl::plus<>{}(Input[2], 2);
    Output[5] = sycl::multiplies<float>{}(Input[2], Input[3]);
  });
}

#ifndef __SYCL_DEVICE_ONLY__
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int Argc, char **Argv) {
  auto Devices = sycl::device::get_devices(sycl::info::device_type::gpu);
  unsigned Device = Argc > 1 ? std::stoul(Argv[1]) : 0;
  sycl::queue Queue(Devices.at(Device), sycl::property::queue::in_order{});
  const std::array<unsigned, 2> Input{13, 6};
  const std::array<unsigned, 18> Expected{19, 19, 78, 78, 4, 4, 15, 15, 11,
                                         11, 1,  1,  1,  1, 6, 6, 13, 13};
  std::array<unsigned, 18> Actual{};
  unsigned *DeviceInput = sycl::malloc_device<unsigned>(Input.size(), Queue);
  unsigned *DeviceOutput = sycl::malloc_device<unsigned>(Actual.size(), Queue);
  if (!DeviceInput || !DeviceOutput)
    throw std::runtime_error("integer device allocation failed");
  Queue.memcpy(DeviceInput, Input.data(), sizeof(Input));
  submitIntegerFunctionObjects(Queue, DeviceInput, DeviceOutput);
  Queue.memcpy(Actual.data(), DeviceOutput, sizeof(Actual)).wait_and_throw();
  sycl::free(DeviceInput, Queue);
  sycl::free(DeviceOutput, Queue);
  assert(Actual == Expected);

  const std::array<float, 4> FloatInput{-0.0f, 0.0f, 3.25f, -8.0f};
  std::array<float, 6> FloatActual{};
  float *DeviceFloatInput = sycl::malloc_device<float>(FloatInput.size(), Queue);
  float *DeviceFloatOutput = sycl::malloc_device<float>(FloatActual.size(), Queue);
  if (!DeviceFloatInput || !DeviceFloatOutput)
    throw std::runtime_error("floating-point device allocation failed");
  Queue.memcpy(DeviceFloatInput, FloatInput.data(), sizeof(FloatInput));
  submitFloatingPointFunctionObjects(Queue, DeviceFloatInput, DeviceFloatOutput);
  Queue.memcpy(FloatActual.data(), DeviceFloatOutput, sizeof(FloatActual))
      .wait_and_throw();
  sycl::free(DeviceFloatInput, Queue);
  sycl::free(DeviceFloatOutput, Queue);
  for (unsigned I = 0; I != 4; ++I) {
    assert(FloatActual[I] == 0.0f);
    assert(std::signbit(FloatActual[I]) == (I < 2));
  }
  assert(FloatActual[4] == 5.25f);
  assert(FloatActual[5] == -26.0f);
  std::cout << "PASS: SYCL function objects on "
            << Devices[Device].get_info<sycl::info::device::name>() << '\n';
}
#endif
