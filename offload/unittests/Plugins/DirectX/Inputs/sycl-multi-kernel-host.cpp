#include <sycl/sycl.hpp>

void submitKernels(sycl::queue &Queue, unsigned *Output, unsigned Count);

#ifndef __SYCL_DEVICE_ONLY__
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int Argc, char **Argv) {
  try {
    std::vector<sycl::device> Devices;
    for (auto Device : sycl::device::get_devices())
      if (Device.get_backend() == sycl::backend::ext_llvm_directx)
        Devices.push_back(Device);
    unsigned Device = Argc > 1 ? std::stoul(Argv[1]) : 0;
    sycl::queue Queue(Devices.at(Device), sycl::property::queue::in_order{});
    constexpr unsigned Count = 67;
    std::vector<unsigned> Actual(Count + 4, 0xfefefefe);
    unsigned *Output = sycl::malloc_device<unsigned>(Actual.size(), Queue);
    if (!Output)
      throw std::runtime_error("device allocation failed");
    Queue.memcpy(Output, Actual.data(), Actual.size() * sizeof(unsigned));
    submitKernels(Queue, Output, Count);
    Queue.memcpy(Actual.data(), Output, Actual.size() * sizeof(unsigned))
        .wait_and_throw();
    sycl::free(Output, Queue);
    for (unsigned I = 0; I < Count; ++I)
      if (Actual[I] != (I + 1) * 3)
        throw std::runtime_error("multi-kernel result mismatch");
    for (unsigned I = Count; I < Actual.size(); ++I)
      if (Actual[I] != 0xfefefefe)
        throw std::runtime_error("trailing guard overwritten");
    std::cout << "PASS: separately compiled SYCL kernels on "
              << Devices[Device].get_info<sycl::info::device::name>() << '\n';
    return 0;
  } catch (const std::exception &Error) {
    std::cerr << Error.what() << '\n';
    return 1;
  }
}
#endif
