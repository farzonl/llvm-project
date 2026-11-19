//===--- DirectXTest.cpp - DirectX discovery tests
//--------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <OffloadAPI.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace {

testing::AssertionResult succeeded(ol_result_t Result) {
  if (!Result)
    return testing::AssertionSuccess();
  return testing::AssertionFailure() << Result->Details;
}

class ScopedEnvironment {
  const char *Name;
  std::optional<std::string> Previous;

  static void set(const char *Name, const char *Value) {
#ifdef _WIN32
    ASSERT_EQ(_putenv_s(Name, Value ? Value : ""), 0);
#else
    ASSERT_EQ(Value ? setenv(Name, Value, 1) : unsetenv(Name), 0);
#endif
  }

public:
  ScopedEnvironment(const char *Name, const char *Value) : Name(Name) {
    if (const char *Old = std::getenv(Name))
      Previous = Old;
    set(Name, Value);
  }
  ~ScopedEnvironment() { set(Name, Previous ? Previous->c_str() : nullptr); }
};

class DirectXTest : public testing::Test {
protected:
  void SetUp() override {
    ol_platform_backend_t Backend = OL_PLATFORM_BACKEND_DIRECTX;
    ol_init_args_t Args = OL_INIT_ARGS_INIT;
    Args.NumPlatforms = 1;
    Args.Platforms = &Backend;
    ASSERT_TRUE(succeeded(olInit(&Args)));
  }

  void TearDown() override { EXPECT_TRUE(succeeded(olShutDown())); }
};

TEST_F(DirectXTest, MissingRuntimeIsAnError) {
  ScopedEnvironment Missing("OFFLOAD_DIRECTX_DXCORE_LIBRARY",
                            "offload-nonexistent-directx-library");
  auto Result = olIterateDevices(
      [](ol_device_handle_t, void *) { return true; }, nullptr);
  ASSERT_NE(Result, nullptr);
  EXPECT_EQ(Result->Code, OL_ERRC_BACKEND_FAILURE);
  EXPECT_NE(std::string(Result->Details).find("OFFLOAD_DIRECTX_DXCORE_LIBRARY"),
            std::string::npos);
}

TEST_F(DirectXTest, DiscoversDevicesAndRestarts) {
#ifndef _WIN32
  if (!std::getenv("OFFLOAD_DIRECTX_DXCORE_LIBRARY") ||
      !std::getenv("OFFLOAD_DIRECTX_D3D12_LIBRARY"))
    GTEST_SKIP() << "Set DirectX runtime paths to run Linux discovery tests";
#endif

  std::vector<std::string> ExpectedIdentities;
  for (unsigned Iteration = 0; Iteration != 3; ++Iteration) {
    std::vector<ol_device_handle_t> Devices;
    ASSERT_TRUE(succeeded(olIterateDevices(
        [](ol_device_handle_t Device, void *Data) {
          static_cast<std::vector<ol_device_handle_t> *>(Data)->push_back(
              Device);
          return true;
        },
        &Devices)));

    std::vector<std::string> Identities;
    for (auto Device : Devices) {
      ol_platform_handle_t Platform;
      ASSERT_TRUE(succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_PLATFORM,
                                            sizeof(Platform), &Platform)));
      ol_platform_backend_t Backend;
      ASSERT_TRUE(succeeded(olGetPlatformInfo(
          Platform, OL_PLATFORM_INFO_BACKEND, sizeof(Backend), &Backend)));
      if (Backend != OL_PLATFORM_BACKEND_DIRECTX)
        continue;

      for (auto Property : {OL_DEVICE_INFO_PRODUCT_NAME, OL_DEVICE_INFO_UID}) {
        size_t Size;
        ASSERT_TRUE(succeeded(olGetDeviceInfoSize(Device, Property, &Size)));
        ASSERT_GT(Size, 1u);
        std::string Value(Size, '\0');
        ASSERT_TRUE(
            succeeded(olGetDeviceInfo(Device, Property, Size, Value.data())));
        EXPECT_EQ(Value.back(), '\0');
        Identities.push_back(std::move(Value));
      }

      void *NativeDevice = nullptr;
      ASSERT_TRUE(
          succeeded(olGetDeviceInfo(Device, OL_DEVICE_INFO_NATIVE_HANDLE,
                                    sizeof(NativeDevice), &NativeDevice)));
      EXPECT_NE(NativeDevice, nullptr);

      ol_context_handle_t Contexts[2] = {};
      for (auto &Context : Contexts)
        ASSERT_TRUE(succeeded(olCreateContext(1, &Device, &Context)));
      EXPECT_NE(Contexts[0], Contexts[1]);
      ASSERT_TRUE(succeeded(olDestroyContext(Contexts[0])));

      void *ContextDevice = nullptr;
      ASSERT_TRUE(
          succeeded(olGetContextInfo(Contexts[1], OL_CONTEXT_INFO_NATIVE_HANDLE,
                                     sizeof(ContextDevice), &ContextDevice)));
      EXPECT_EQ(ContextDevice, NativeDevice);

      ol_device_handle_t RepeatedDevices[] = {Device, Device};
      ol_context_handle_t RejectedContext = nullptr;
      auto Result = olCreateContext(2, RepeatedDevices, &RejectedContext);
      ASSERT_NE(Result, nullptr);
      EXPECT_EQ(Result->Code, OL_ERRC_UNSUPPORTED);
      EXPECT_EQ(RejectedContext, nullptr);

      void *Allocation = nullptr;
      Result = olMemAlloc(Contexts[1], Device, OL_ALLOC_TYPE_DEVICE, 16,
                          &Allocation);
      ASSERT_NE(Result, nullptr);
      EXPECT_EQ(Result->Code, OL_ERRC_UNSUPPORTED);
      EXPECT_EQ(Allocation, nullptr);

      ol_queue_handle_t Queue = nullptr;
      Result = olCreateQueue(Contexts[1], Device, &Queue);
      ASSERT_NE(Result, nullptr);
      EXPECT_EQ(Result->Code, OL_ERRC_UNSUPPORTED);
      EXPECT_EQ(Queue, nullptr);

      ASSERT_TRUE(succeeded(olDestroyContext(Contexts[1])));
    }
    ASSERT_FALSE(Identities.empty()) << "No DirectX adapters were discovered";
    if (Iteration == 0)
      ExpectedIdentities = Identities;
    else
      EXPECT_EQ(Identities, ExpectedIdentities);
    if (Iteration != 2) {
      ASSERT_TRUE(succeeded(olShutDown()));
      SetUp();
    }
  }
}

} // namespace
