//===- GlobalHandlerTest.cpp - Tests for plugin global handling
//------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GlobalHandler.h"
#include "PluginInterface.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::omp::target::plugin;

extern "C" GenericPluginTy *createPlugin_host();

namespace {

class GlobalHandlerTest : public testing::Test {
protected:
  std::unique_ptr<GenericPluginTy> Plugin{createPlugin_host()};
  std::unique_ptr<GenericDeviceTy> Device{Plugin->createDevice(*Plugin, 0, 1)};
  std::unique_ptr<GenericGlobalHandlerTy> Handler{
      Plugin->createGlobalHandler()};

  DeviceImageTy makeImage(StringRef Contents) {
    return DeviceImageTy(0, *Device, MemoryBuffer::getMemBufferCopy(Contents));
  }

  DeviceImageTy makeELFImage() {
    object::ELF64LE::Ehdr Header{};
    Header.e_ident[ELF::EI_MAG0] = 0x7f;
    Header.e_ident[ELF::EI_MAG1] = 'E';
    Header.e_ident[ELF::EI_MAG2] = 'L';
    Header.e_ident[ELF::EI_MAG3] = 'F';
    Header.e_ident[ELF::EI_CLASS] = ELF::ELFCLASS64;
    Header.e_ident[ELF::EI_DATA] = ELF::ELFDATA2LSB;
    Header.e_ident[ELF::EI_VERSION] = ELF::EV_CURRENT;
    Header.e_type = ELF::ET_REL;
    Header.e_machine = ELF::EM_X86_64;
    Header.e_version = ELF::EV_CURRENT;
    Header.e_ehsize = sizeof(Header);
    return makeImage(
        StringRef(reinterpret_cast<const char *>(&Header), sizeof(Header)));
  }
};

TEST_F(GlobalHandlerTest, RejectsNonELFImages) {
  for (StringRef Contents :
       {StringRef(), StringRef("DXBC"), StringRef("\x03\x02\x23\x07")}) {
    auto Image = makeImage(Contents);
    auto Object = Handler->getELFObjectFile(Image);
    EXPECT_EQ(errorToErrorCode(Object.takeError()),
              error::make_error_code(error::ErrorCode::UNSUPPORTED));
  }
}

TEST_F(GlobalHandlerTest, RejectsNonELFGlobalOperations) {
  auto Image = makeImage("DXBC");
  GlobalTy Global("global");
  EXPECT_EQ(errorToErrorCode(
                Handler->getGlobalMetadataFromImage(*Device, Image, Global)),
            error::make_error_code(error::ErrorCode::UNSUPPORTED));

  bool Called = false;
  EXPECT_EQ(errorToErrorCode(Handler->iterateSymbols(
                Image, SymbolKindTy::Kernel,
                [&](StringRef) { return Called = true; })),
            error::make_error_code(error::ErrorCode::UNSUPPORTED));
  EXPECT_FALSE(Called);
}

TEST_F(GlobalHandlerTest, NonELFHasNoSymbolsOrProfiling) {
  auto Image = makeImage("DXBC");
  EXPECT_FALSE(Handler->isSymbolInImage(*Device, Image, "global"));
  auto Profile = Handler->readProfilingGlobals(*Device, Image);
  ASSERT_TRUE(static_cast<bool>(Profile)) << toString(Profile.takeError());
  EXPECT_TRUE(Profile->empty());
}

TEST_F(GlobalHandlerTest, ParsesELFImages) {
  auto Image = makeELFImage();
  auto Object = Handler->getELFObjectFile(Image);
  ASSERT_TRUE(static_cast<bool>(Object)) << toString(Object.takeError());
  EXPECT_TRUE((*Object)->isELF());

  bool Called = false;
  EXPECT_FALSE(errorToErrorCode(Handler->iterateSymbols(
      Image, SymbolKindTy::Kernel, [&](StringRef) { return Called = true; })));
  EXPECT_FALSE(Called);
  EXPECT_FALSE(Handler->isSymbolInImage(*Device, Image, "global"));
  auto Profile = Handler->readProfilingGlobals(*Device, Image);
  ASSERT_TRUE(static_cast<bool>(Profile)) << toString(Profile.takeError());
  EXPECT_TRUE(Profile->empty());
}

TEST_F(GlobalHandlerTest, RejectsMalformedELFImages) {
  auto ELFImage = makeELFImage();
  auto Image = makeImage(ELFImage.getMemoryBuffer().getBuffer().take_front(18));
  auto Object = Handler->getELFObjectFile(Image);
  EXPECT_EQ(errorToErrorCode(Object.takeError()),
            error::make_error_code(error::ErrorCode::INVALID_BINARY));
}

} // namespace
