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

template <int D> class WorkgroupGeometry;

template <int D>
void launch(sycl::queue &Queue, sycl::buffer<unsigned> &Buffer,
            sycl::nd_range<D> Range) {
  Queue.submit([&](sycl::handler &Handler) {
    auto Output = Buffer.get_access(Handler);
    Handler.parallel_for<WorkgroupGeometry<D>>(
        Range, [=](sycl::nd_item<D> Item) {
          Output[Item.get_global_linear_id()] = static_cast<unsigned>(
              Item.get_local_linear_id() + 1024 * Item.get_group_linear_id() +
              65536 * Item.get_local_range().size());
        });
  });
}

template void launch<1>(sycl::queue &, sycl::buffer<unsigned> &,
                        sycl::nd_range<1>);
template void launch<2>(sycl::queue &, sycl::buffer<unsigned> &,
                        sycl::nd_range<2>);
template void launch<3>(sycl::queue &, sycl::buffer<unsigned> &,
                        sycl::nd_range<3>);

#ifndef __SYCL_DEVICE_ONLY__
#  include <array>
#  include <cassert>
#  include <cstdlib>
#  include <iostream>
#  include <string>
#  include <vector>

template <int D> void check(sycl::queue &Queue, sycl::nd_range<D> Range) {
  auto Global = Range.get_global_range();
  auto Local = Range.get_local_range();
  std::vector<unsigned> Result(Global.size(), 0);
  {
    sycl::buffer<unsigned> Buffer(Result.data(), sycl::range<1>(Result.size()));
    launch(Queue, Buffer, Range);
  }
  for (size_t I = 0; I < Result.size(); ++I) {
    std::array<size_t, D> Coordinates{};
    size_t Remaining = I;
    for (int Axis = D - 1; Axis >= 0; --Axis) {
      Coordinates[Axis] = Remaining % Global[Axis];
      Remaining /= Global[Axis];
    }
    size_t LocalID = 0, GroupID = 0;
    for (int Axis = 0; Axis < D; ++Axis) {
      LocalID = LocalID * Local[Axis] + Coordinates[Axis] % Local[Axis];
      GroupID = GroupID * (Global[Axis] / Local[Axis]) +
                Coordinates[Axis] / Local[Axis];
    }
    assert(Result[I] == LocalID + 1024 * GroupID + 65536 * Local.size());
  }
}

void compilerPath(const char *Value) {
#  ifdef _WIN32
  int Status = _putenv_s("OFFLOAD_DIRECTX_COMPILER_PATH", Value ? Value : "");
#  else
  int Status = Value ? setenv("OFFLOAD_DIRECTX_COMPILER_PATH", Value, 1)
                     : unsetenv("OFFLOAD_DIRECTX_COMPILER_PATH");
#  endif
  if (Status != 0) {
    std::cerr << "Cannot update DirectX compiler path\n";
    std::abort();
  }
}

class CompilerPathOverride {
  std::string Saved;
  bool HadPath;

public:
  explicit CompilerPathOverride(const char *Value) {
#  ifdef _WIN32
    char *Path = nullptr;
    size_t Size = 0;
    if (_dupenv_s(&Path, &Size, "OFFLOAD_DIRECTX_COMPILER_PATH") != 0) {
      std::cerr << "Cannot read DirectX compiler path\n";
      std::abort();
    }
#  else
    const char *Path = std::getenv("OFFLOAD_DIRECTX_COMPILER_PATH");
#  endif
    HadPath = Path != nullptr;
    Saved = Path ? Path : "";
#  ifdef _WIN32
    std::free(Path);
#  endif
    compilerPath(Value);
  }
  ~CompilerPathOverride() { compilerPath(HadPath ? Saved.c_str() : nullptr); }
};

int main(int Argc, char **Argv) {
  try {
    auto Devices = sycl::device::get_devices(sycl::info::device_type::gpu);
    auto Device = Devices.at(Argc > 1 ? std::stoul(Argv[1]) : 0);
    sycl::queue Queue(Device);
    {
      CompilerPathOverride Override("nonexistent-directx-compiler-directory");
      check(Queue, sycl::nd_range<1>{sycl::range<1>{9}, sycl::range<1>{1}});
    }
    check(Queue, sycl::nd_range<1>{sycl::range<1>{9}, sycl::range<1>{3}});
    {
      CompilerPathOverride Override("nonexistent-directx-compiler-directory");
      check(Queue, sycl::nd_range<1>{sycl::range<1>{15}, sycl::range<1>{3}});
      if (Device.get_backend() == sycl::backend::ext_llvm_directx) {
        bool Rejected = false;
        try {
          check(Queue,
                sycl::nd_range<1>{sycl::range<1>{14}, sycl::range<1>{7}});
        } catch (const sycl::exception &Error) {
          if (std::string(Error.what()).find("specialization tool") ==
              std::string::npos)
            throw;
          Rejected = true;
        }
        assert(Rejected);
      }
    }
    check(Queue, sycl::nd_range<1>{sycl::range<1>{15}, sycl::range<1>{5}});
    check(Queue, sycl::nd_range<2>{sycl::range<2>{4, 9}, sycl::range<2>{2, 3}});
    check(Queue,
          sycl::nd_range<3>{sycl::range<3>{4, 6, 4}, sycl::range<3>{2, 3, 2}});
    std::cout << Device.get_info<sycl::info::device::name>()
              << ": specialized 1D/2D/3D geometry and cached dispatch passed\n";
  } catch (const sycl::exception &Error) {
    std::cerr << "Workgroup test failed: " << Error.what() << '\n';
    return 1;
  }
}
#endif
