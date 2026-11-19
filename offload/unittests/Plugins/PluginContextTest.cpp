//===- PluginContextTest.cpp - Plugin context tests
//------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PluginInterface.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::omp::target::plugin;

extern "C" GenericPluginTy *createPlugin_host();

namespace {

struct TestContextTy final : PluginContextTy {
  bool FailInit;

  TestContextTy(GenericPluginTy &Plugin, bool FailInit)
      : PluginContextTy(Plugin, {}), FailInit(FailInit) {}

  Error initAsyncInfoImpl(GenericDeviceTy &, AsyncInfoWrapperTy &) override {
    if (FailInit)
      return Plugin::error(error::ErrorCode::UNSUPPORTED,
                           "queue initialization is unsupported");
    return Error::success();
  }

  Expected<PluginAllocInfoTy> getAllocInfo(const void *) override {
    return Plugin::error(error::ErrorCode::NOT_FOUND, "no allocations");
  }
};

TEST(PluginContextTest, AsyncInfoOwnership) {
  std::unique_ptr<GenericPluginTy> Plugin{createPlugin_host()};
  std::unique_ptr<GenericDeviceTy> Device{Plugin->createDevice(*Plugin, 0, 1)};
  for (bool FailInit : {true, false}) {
    SCOPED_TRACE(FailInit);
    TestContextTy Context(*Plugin, FailInit);
    __tgt_async_info *AsyncInfo = nullptr;
    EXPECT_EQ(errorToErrorCode(Context.initAsyncInfo(*Device, &AsyncInfo)),
              FailInit ? error::make_error_code(error::ErrorCode::UNSUPPORTED)
                       : std::error_code());
    EXPECT_EQ(AsyncInfo == nullptr, FailInit);
    delete AsyncInfo;
  }
}

} // namespace
