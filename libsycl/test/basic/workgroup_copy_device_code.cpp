//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// RUN: %clangxx -fsycl -fsycl-device-only --offload-targets=dxil-pc-shadermodel6.0-compute -O2 -S -emit-llvm %s -o - | FileCheck %s
// RUN: not %clangxx -O2 -c -DTEST_HOST %s -o %t.o 2>&1 | FileCheck %s --check-prefix=HOST
// RUN: not %clangxx -std=c++17 -fsyntax-only -DTEST_DEFAULT %s 2>&1 | FileCheck %s --check-prefix=DEFAULT

#include <sycl/sycl.hpp>

#if defined(TEST_DEFAULT)
// DEFAULT: calling a private constructor of class
void unsupportedConstruction() { sycl::device_event Event{}; }
#elif defined(TEST_HOST)
// HOST-DAG: Device event waits are currently supported only in DirectX SYCL device code
// HOST-DAG: Workgroup copies are currently supported only in DirectX SYCL device code
void unsupportedWait(sycl::device_event Event) { Event.wait(); }
void unsupportedCopy(sycl::group<1> Group, sycl::decorated_local_ptr<int> Local,
                     sycl::decorated_global_ptr<const int> Global) {
  Group.async_work_group_copy(Local, Global, 17);
}
#else
// CHECK-COUNT-4: call void @llvm.dx.all.memory.barrier.with.group.sync()
// CHECK-NOT: call void @llvm.dx.all.memory.barrier.with.group.sync()
void launchCopy(sycl::queue &Queue, sycl::buffer<int> &Input,
                sycl::buffer<int> &Output) {
  Queue.submit([&](sycl::handler &Handler) {
    sycl::local_accessor<int> Storage(sycl::range<1>(17), Handler);
    sycl::accessor Read(Input, Handler, sycl::read_only);
    sycl::accessor Write(Output, Handler, sycl::write_only);
    Handler.parallel_for<class CopySynchronization>(
        sycl::nd_range<1>(sycl::range<1>(8), sycl::range<1>(4)),
        [=](sycl::nd_item<1> Item) {
          auto Local = Storage.get_multi_ptr<sycl::access::decorated::yes>();
          auto Source = Read.get_multi_ptr<sycl::access::decorated::yes>();
          auto Event =
              Item.get_group().async_work_group_copy(Local, Source, 17);
          if (Item.get_local_linear_id() == 0)
            Event.wait();
          Item.wait_for(Event, Event);
          sycl::decorated_local_ptr<const int> ConstLocal = Local;
          auto Destination =
              Write.get_multi_ptr<sycl::access::decorated::yes>();
          auto Result =
              Item.async_work_group_copy(Destination, ConstLocal, 17, 2);
          Result.wait();
        });
  });
}
#endif
