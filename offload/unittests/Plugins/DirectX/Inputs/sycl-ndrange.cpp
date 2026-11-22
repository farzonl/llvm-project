// Compile host and device from this source using the directx-sycl example's
// pipeline, with -DTEST_DIMS=1, 2, or 3 in both compilations.
// This native integration probe uses real libsycl work-item/range builders.

#include <sycl/sycl.hpp>

#ifndef TEST_DIMS
#define TEST_DIMS 1
#endif

void submitGeometry(sycl::queue &Queue, unsigned *Output,
                    sycl::nd_range<TEST_DIMS> Range) {
  Queue.parallel_for<class NDRangeGeometry>(
      Range,
      [=](sycl::nd_item<TEST_DIMS> Item) __attribute__((reqd_work_group_size(
          4, TEST_DIMS > 1 ? 2 : 1, TEST_DIMS > 2 ? 2 : 1))) {
        size_t Index = Item.get_global_linear_id();
        unsigned *Record = Output + Index * 22;
        Record[0] = static_cast<unsigned>(Index);
        Record[1] = static_cast<unsigned>(Item.get_local_linear_id());
        Record[2] = static_cast<unsigned>(Item.get_group_linear_id());
        Record[3] = static_cast<unsigned>(Item.get_global_range().size());
        for (int D = 0; D < TEST_DIMS; ++D) {
          size_t ID = Item.get_global_id(D);
          Record[4 + D * 6] = static_cast<unsigned>(ID);
          Record[5 + D * 6] = static_cast<unsigned>(ID >> 32);
          Record[6 + D * 6] = static_cast<unsigned>(Item.get_local_id(D));
          Record[7 + D * 6] = static_cast<unsigned>(Item.get_group(D));
          Record[8 + D * 6] = static_cast<unsigned>(Item.get_global_range(D));
          Record[9 + D * 6] = static_cast<unsigned>(Item.get_group_range(D));
        }
      });
}

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
    sycl::range<TEST_DIMS> Global, Local;
    sycl::id<TEST_DIMS> Offset;
    for (int D = 0; D < TEST_DIMS; ++D) {
      Local[D] = D == TEST_DIMS - 1 ? 4 : 2;
      Global[D] = Local[D] * (D + 2);
      Offset[D] = (size_t{1} << 32) + D * 17 + 3;
    }
    std::vector<unsigned> Actual(Global.size() * 22 + 4, 0xfefefefe);
    unsigned *Output = sycl::malloc_device<unsigned>(Actual.size(), Queue);
    if (!Output)
      throw std::runtime_error("device allocation failed");
    for (unsigned Iteration = 0; Iteration < 3; ++Iteration) {
      Queue.memcpy(Output, Actual.data(), Actual.size() * sizeof(unsigned));
      // Deprecated SYCL offsets exercise geometry also needed by OpenCL.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      submitGeometry(Queue, Output,
                     sycl::nd_range<TEST_DIMS>{Global, Local, Offset});
#pragma clang diagnostic pop
      Queue.memcpy(Actual.data(), Output, Actual.size() * sizeof(unsigned))
          .wait_and_throw();
      for (size_t I = 0; I < Global.size(); ++I) {
        const unsigned *Record = Actual.data() + I * 22;
        size_t Remainder = I;
        size_t LocalLinear = 0, GroupLinear = 0;
        sycl::id<TEST_DIMS> ID;
        for (int D = TEST_DIMS - 1; D >= 0; --D) {
          ID[D] = Remainder % Global[D];
          Remainder /= Global[D];
        }
        for (int D = 0; D < TEST_DIMS; ++D) {
          size_t FullID = ID[D] + Offset[D];
          if (Record[4 + D * 6] != static_cast<unsigned>(FullID) ||
              Record[5 + D * 6] != FullID >> 32 ||
              Record[6 + D * 6] != ID[D] % Local[D] ||
              Record[7 + D * 6] != ID[D] / Local[D] ||
              Record[8 + D * 6] != Global[D] ||
              Record[9 + D * 6] != Global[D] / Local[D])
            throw std::runtime_error("work-item geometry mismatch");
          LocalLinear = LocalLinear * Local[D] + ID[D] % Local[D];
          GroupLinear = GroupLinear * (Global[D] / Local[D]) + ID[D] / Local[D];
        }
        if (Record[0] != I || Record[1] != LocalLinear ||
            Record[2] != GroupLinear || Record[3] != Global.size())
          throw std::runtime_error("linear index or range mismatch");
      }
      for (size_t I = Global.size() * 22; I < Actual.size(); ++I)
        if (Actual[I] != 0xfefefefe)
          throw std::runtime_error("trailing guard overwritten");
      Offset[0] += 5;
    }
    sycl::free(Output, Queue);
    std::cout << "PASS: " << TEST_DIMS << "D SYCL geometry on "
              << Devices[Device].get_info<sycl::info::device::name>() << '\n';
    return 0;
  } catch (const std::exception &Error) {
    std::cerr << Error.what() << '\n';
    return 1;
  }
}
#endif
