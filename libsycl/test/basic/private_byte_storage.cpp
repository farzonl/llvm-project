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

#include <cstdint>
#include <cstdlib>
#include <iostream>

class PrivateByteStorage;
struct PointerWrapper {
  void *Value;
};

int main(int Argc, char **Argv) {
  const auto Devices = sycl::device::get_devices();
  char *End = nullptr;
  const auto Index = Argc > 1 ? std::strtoul(Argv[1], &End, 10) : 0;
  if (Argc > 1 && (!*Argv[1] || *End)) {
    std::cerr << "Invalid device index\n";
    return 1;
  }
  if (Index >= Devices.size()) {
    std::cerr << "Invalid device index\n";
    return 1;
  }
  sycl::queue Queue(Devices[Index]);
  int Expected[] = {8, 7, 6, 5, 4, 3, 2, 1, static_cast<int>(sizeof(void *))};
  int Results[] = {0, 0, 0};
  {
    sycl::buffer<int> Input(Expected, sycl::range<1>(9));
    sycl::buffer<int> Output(Results, sycl::range<1>(3));
    Queue.submit([&](sycl::handler &Handler) {
      sycl::accessor Read(Input, Handler, sycl::read_only);
      sycl::accessor Write(Output, Handler, sycl::write_only);
      Handler.single_task<PrivateByteStorage>([=] {
        const int Count = Read[8];
        void *Pointer =
            reinterpret_cast<void *>(std::uint64_t{0x0102030405060708});
        PointerWrapper Wrapper{Pointer};
        auto *Bytes = reinterpret_cast<unsigned char *>(&Pointer);
        bool PointerMatches = true;
        for (int I = 0; I < Count; ++I)
          PointerMatches &= *Bytes++ == Read[I];
        Bytes = reinterpret_cast<unsigned char *>(&Wrapper);
        bool WrapperMatches = true;
        for (int I = 0; I < Count; ++I)
          WrapperMatches &= *Bytes++ == Read[I];
        std::uint64_t Value = 0;
        for (int I = Count - 1; I >= 0; --I)
          Value = (Value << 8) | static_cast<unsigned int>(Read[I]);
        Bytes = reinterpret_cast<unsigned char *>(&Value);
        bool IntegerMatches = true;
        for (int I = 0; I < Count; ++I)
          IntegerMatches &= *Bytes++ == Read[I];
        Write[0] = PointerMatches;
        Write[1] = WrapperMatches;
        Write[2] = IntegerMatches;
      });
    });
    Queue.wait_and_throw();
  }
  for (int I = 0; I < 3; ++I)
    if (Results[I] != 1) {
      std::cerr << "Private byte-storage check " << I << " failed\n";
      return 1;
    }
  std::cout << "Private byte storage passed on "
            << Devices[Index].get_info<sycl::info::device::name>() << '\n';
}
