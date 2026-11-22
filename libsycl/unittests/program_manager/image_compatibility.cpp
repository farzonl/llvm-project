//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Tests for matching a registered device image against a device: only the
/// architecture component of the image's target triple takes part in it.
///
//===----------------------------------------------------------------------===//

#include <common/scoped_binary_registration.hpp>
#include <common/unittests_helper.hpp>
#include <mock/helpers.hpp>

#include <detail/context_impl.hpp>
#include <detail/device_impl.hpp>
#include <detail/program_manager.hpp>

#include <sycl/__impl/device.hpp>
#include <sycl/__impl/exception.hpp>
#include <sycl/__impl/property_list.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace sycl;
using namespace ::testing;

namespace {

/// Creates a context over a single device, bypassing sycl::context, which
/// exposes a platform's default context only.
std::shared_ptr<detail::ContextImpl> createContext(const device &Device) {
  std::vector<detail::DeviceImpl *> Devices = {detail::getSyclObjImpl(Device)};
  return detail::ContextImpl::create(
      std::move(Devices), detail::defaultAsyncHandler, property_list{});
}

/// Allows the liboffload calls that these tests do not assert on, so that
/// context and program lifetime does not produce uninteresting call warnings.
void allowContextAndProgramLifetimeCalls(mock::MockLiboffload &Mock) {
  EXPECT_CALL(Mock, olCreateContext(_, _, _)).Times(AnyNumber());
  EXPECT_CALL(Mock, olDestroyContext(_)).Times(AnyNumber());
  EXPECT_CALL(Mock, olCreateProgram(_, _, _, _, _)).Times(AnyNumber());
  EXPECT_CALL(Mock, olDestroyProgram(_)).Times(AnyNumber());
  EXPECT_CALL(Mock, olGetSymbol(_, _, _, _)).Times(AnyNumber());
}

ol_symbol_handle_t
getKernel(const std::shared_ptr<detail::ContextImpl> &Context,
          const device &Device, std::string_view KernelName) {
  detail::ProgramAndKernelManager &Manager =
      detail::ProgramAndKernelManager::getInstance();
  return Manager.getOrCreateKernel(Manager.getDeviceKernelInfo(KernelName),
                                   Context, *detail::getSyclObjImpl(Device));
}

} // namespace

// An image whose triple carries an extra environment component is still usable.
TEST(ImageCompatibility, ArchitectureAloneSelectsImage) {
  mock::MockWrapper Mock;
  allowContextAndProgramLifetimeCalls(Mock.get());

  const std::string KernelName = "kernel";
  sycl::unittests::ScopedKernelRegistration Registration(
      KernelName, "spirv64-unknown-unknown-elf");

  const device Device;
  std::shared_ptr<detail::ContextImpl> Context = createContext(Device);

  EXPECT_NE(getKernel(Context, Device, KernelName), nullptr);
}

// An image built for another architecture must not be handed to the device.
TEST(ImageCompatibility, ImageForAnotherArchitectureIsRejected) {
  mock::MockWrapper Mock;
  allowContextAndProgramLifetimeCalls(Mock.get());

  const std::string KernelName = "kernel";
  sycl::unittests::ScopedKernelRegistration Registration(
      KernelName, "spirv32-unknown-unknown");

  const device Device;
  std::shared_ptr<detail::ContextImpl> Context = createContext(Device);

  EXPECT_THAT([&]() { getKernel(Context, Device, KernelName); },
              Throws<sycl::exception>(AllOf(
                  Property(&sycl::exception::what,
                           HasSubstr("No compatible image for " + KernelName)),
                  Property(&sycl::exception::code, Eq(sycl::errc::runtime)))));
}

TEST(ImageCompatibility, DirectXValidationAndLoadingReceivePackagedImage) {
  unittests::UnittestsHelper Helper;
  auto &Mock = Helper.Mock;
  allowContextAndProgramLifetimeCalls(Mock.get());
  ON_CALL(Mock.get(), olGetPlatformInfo(_, OL_PLATFORM_INFO_BACKEND, _, _))
      .WillByDefault([](ol_platform_handle_t, ol_platform_info_t, size_t,
                        void *Value) -> ol_result_t {
        *static_cast<ol_platform_backend_t *>(Value) =
            OL_PLATFORM_BACKEND_DIRECTX;
        return OL_SUCCESS;
      });
  std::array<llvm::StringRef, 1> Names = {"kernel"};
  llvm::SmallString<0> Symbols;
  llvm::offloading::sycl::writeSymbolTable(Names, Symbols);
  auto Image = sycl::unittests::createSYCLImage(
      Symbols, llvm::object::IMG_DXIL, llvm::object::OFK_SYCL,
      "dxil-pc-shadermodel6.0-compute");
  llvm::StringRef Manifest =
      R"({"entry":"kernel","capture":{"size":8,"pointer_size":8,"offsets":[0]}})";
  Image.StringData["directx.manifest"] = Manifest;
  auto Binary = llvm::object::OffloadBinary::write(Image);
  struct TestManager : detail::ProgramAndKernelManager {
  } Manager;
  Manager.registerFatBin(Binary.data(), Binary.size());
  const device Device;
  auto Context = createContext(Device);
  auto CheckPackage = [&](const void *Data, size_t Size) {
    auto Entries = llvm::object::OffloadBinary::create(llvm::MemoryBufferRef(
        llvm::StringRef(static_cast<const char *>(Data), Size), ""));
    ASSERT_TRUE(static_cast<bool>(Entries))
        << llvm::toString(Entries.takeError());
    ASSERT_EQ(Entries->size(), 1u);
    EXPECT_EQ(Entries->front()->getImage(), Image.Image->getBuffer());
    EXPECT_EQ(Entries->front()->getString("directx.manifest"), Manifest);
  };
  bool Valid = false;
  EXPECT_CALL(Mock.get(), olIsValidBinary(_, _, _, _))
      .Times(2)
      .WillRepeatedly([&](ol_device_handle_t, const void *Data, size_t Size,
                          bool *Result) -> ol_result_t {
        CheckPackage(Data, Size);
        *Result = Valid;
        return OL_SUCCESS;
      });
  auto Lookup = [&] {
    return Manager.getOrCreateKernel(Manager.getDeviceKernelInfo("kernel"),
                                     Context, *detail::getSyclObjImpl(Device));
  };
  EXPECT_THROW(Lookup(), sycl::exception);
  Valid = true;
  EXPECT_CALL(Mock.get(), olCreateProgram(_, _, _, _, _))
      .WillOnce([&](ol_context_handle_t, ol_device_handle_t, const void *Data,
                    size_t Size, ol_program_handle_t *Program) -> ol_result_t {
        CheckPackage(Data, Size);
        *Program = mock::createDummyHandle<ol_program_handle_t>();
        return OL_SUCCESS;
      });
  EXPECT_NE(Lookup(), nullptr);
  Manager.unregisterFatBin(Binary.data(), Binary.size());
}
