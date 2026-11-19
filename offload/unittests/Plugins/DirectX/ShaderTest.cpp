//===- ShaderTest.cpp - DirectX shader ABI validation tests
//----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXManifest.h"
#include "OffloadError.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "gtest/gtest.h"

#include <limits>

using namespace llvm;
using namespace llvm::omp::target::plugin::directx;

namespace {

class DirectXShaderTest : public testing::Test {
protected:
  DirectXManifestTy Manifest;
  std::string Bytes;

  void SetUp() override {
    auto Buffer =
        MemoryBuffer::getFile(DIRECTX_TEST_INPUTS_DIR "/manifest.dxil", false);
    ASSERT_TRUE(static_cast<bool>(Buffer)) << Buffer.getError().message();
    Bytes = (*Buffer)->getBuffer().str();
    auto Parsed = DirectXManifestTy::parse(R"({
      "version": 1, "entry": "main", "shader_model": [6, 0],
      "numthreads": [8, 2, 1], "features": ["Int64Ops"],
      "constants": {"register": 4, "space": 2, "size": 16},
      "arguments": [
        {"index": 0, "kind": "srv", "register": 2, "space": 1, "min_size": 4},
        {"index": 1, "kind": "uav", "register": 3, "space": 1, "min_size": 4},
        {"index": 2, "kind": "scalar", "size": 4, "offset": 0},
        {"index": 3, "kind": "scalar", "size": 8, "offset": 8}
      ]})");
    ASSERT_TRUE(static_cast<bool>(Parsed)) << toString(Parsed.takeError());
    Manifest = std::move(*Parsed);
  }

  Error validate(StringRef Data) {
    return Manifest.validateShader(MemoryBufferRef(Data, "test shader"));
  }

  std::string rewriteProgram(function_ref<void(Module &)> Rewrite,
                             StringRef PartName = "DXIL") {
    size_t Offset = part(PartName);
    const char *Program = Bytes.data() + Offset + 8;
    uint32_t BitcodeOffset = support::endian::read32le(Program + 16);
    uint32_t BitcodeSize = support::endian::read32le(Program + 20);
    LLVMContext Context;
    ParserCallbacks Callbacks;
    Callbacks.DataLayout =
        [](StringRef, StringRef) -> std::optional<std::string> { return "e"; };
    auto M = cantFail(parseBitcodeFile(
        MemoryBufferRef(StringRef(Program + 8 + BitcodeOffset, BitcodeSize),
                        "fixture"),
        Context, Callbacks));
    Rewrite(*M);
    SmallString<0> Bitcode;
    raw_svector_ostream OS(Bitcode);
    WriteBitcodeToFile(*M, OS);
    std::string NewProgram(Program, 24);
    support::endian::write32le(NewProgram.data() + 16, 16);
    support::endian::write32le(NewProgram.data() + 20, Bitcode.size());
    NewProgram.append(Bitcode.data(), Bitcode.size());
    while (NewProgram.size() % 4)
      NewProgram.push_back(0);
    support::endian::write32le(NewProgram.data() + 4, NewProgram.size() / 4);

    uint32_t Count = support::endian::read32le(Bytes.data() + 28);
    std::string Result = Bytes.substr(0, 32 + 4 * Count);
    for (uint32_t I = 0; I < Count; ++I) {
      uint32_t OldOffset = support::endian::read32le(Bytes.data() + 32 + 4 * I);
      uint32_t OldSize =
          support::endian::read32le(Bytes.data() + OldOffset + 4);
      StringRef Data = OldOffset == Offset
                           ? StringRef(NewProgram)
                           : StringRef(Bytes).substr(OldOffset + 8, OldSize);
      support::endian::write32le(Result.data() + 32 + 4 * I, Result.size());
      Result.append(Bytes.data() + OldOffset, 4);
      size_t SizeOffset = Result.size();
      Result.resize(SizeOffset + 4);
      support::endian::write32le(Result.data() + SizeOffset, Data.size());
      Result.append(Data.data(), Data.size());
    }
    support::endian::write32le(Result.data() + 24, Result.size());
    return Result;
  }

  size_t part(StringRef Name) {
    uint32_t Count = support::endian::read32le(Bytes.data() + 28);
    for (uint32_t I = 0; I < Count; ++I) {
      uint32_t Offset = support::endian::read32le(Bytes.data() + 32 + 4 * I);
      if (StringRef(Bytes).substr(Offset, 4) == Name)
        return Offset;
    }
    ADD_FAILURE() << "fixture has no " << Name.str() << " part";
    return 0;
  }

  void expectError(Error Err, error::ErrorCode Code, StringRef Message) {
    ASSERT_TRUE(static_cast<bool>(Err));
    handleAllErrors(std::move(Err), [&](const error::OffloadError &Detail) {
      EXPECT_EQ(Detail.convertToErrorCode(), error::make_error_code(Code));
      EXPECT_TRUE(StringRef(Detail.getMessage()).contains(Message))
          << Detail.getMessage();
    });
  }
};

TEST_F(DirectXShaderTest, ValidatesRealCompiledShaderWithoutRuntime) {
  auto Err = validate(Bytes);
  EXPECT_FALSE(static_cast<bool>(Err)) << toString(std::move(Err));
  Manifest.Features |= uint64_t(dxbc::FeatureFlags::Doubles);
  Err = validate(Bytes);
  EXPECT_FALSE(static_cast<bool>(Err)) << toString(std::move(Err));
}

TEST_F(DirectXShaderTest, BufferOnlyShaderDoesNotRequireReflection) {
  auto Buffer =
      MemoryBuffer::getFile(DIRECTX_TEST_INPUTS_DIR "/buffers.dxil", false);
  ASSERT_TRUE(static_cast<bool>(Buffer)) << Buffer.getError().message();
  Bytes = (*Buffer)->getBuffer().str();
  Bytes.replace(part("STAT"), 4, "PRIV");
  Manifest.Constants.reset();
  Manifest.Arguments.resize(2);
  Manifest.Features = 0;
  auto Err = validate(Bytes);
  EXPECT_FALSE(static_cast<bool>(Err)) << toString(std::move(Err));
}

TEST_F(DirectXShaderTest, RejectsEntryModelAndThreadMismatch) {
  auto Original = Manifest;
  Manifest.Entry = "other";
  expectError(validate(Bytes), error::ErrorCode::INVALID_BINARY, "entry point");
  Manifest = Original;
  Manifest.ShaderModel[1] = 1;
  expectError(validate(Bytes), error::ErrorCode::INVALID_BINARY,
              "shader model");
  Manifest = Original;
  Manifest.NumThreads[1] = 1;
  expectError(validate(Bytes), error::ErrorCode::INVALID_BINARY, "numthreads");
}

TEST_F(DirectXShaderTest, RejectsResourceAndConstantMismatch) {
  auto Original = Manifest;
  auto &Buffer =
      std::get<DirectXBufferBindingTy>(Manifest.Arguments[0].Binding);
  Buffer.Register = 1;
  expectError(validate(Bytes), error::ErrorCode::INVALID_BINARY, "binding");
  Manifest = Original;
  std::get<DirectXBufferBindingTy>(Manifest.Arguments[0].Binding).Space = 0;
  expectError(validate(Bytes), error::ErrorCode::INVALID_BINARY, "binding");
  Manifest = Original;
  std::get<DirectXBufferBindingTy>(Manifest.Arguments[0].Binding).Access =
      DirectXBufferBindingTy::Kind::UAV;
  expectError(validate(Bytes), error::ErrorCode::INVALID_BINARY, "binding");
  Manifest = Original;
  Manifest.Constants->Size = 12;
  expectError(validate(Bytes), error::ErrorCode::INVALID_BINARY, "block size");
  Manifest = Original;
  Manifest.Constants->Register = 0;
  expectError(validate(Bytes), error::ErrorCode::INVALID_BINARY, "binding");
  Manifest = Original;
  std::get<DirectXScalarBindingTy>(Manifest.Arguments[2].Binding).Offset = 4;
  expectError(validate(Bytes), error::ErrorCode::INVALID_BINARY,
              "field layout");
  Manifest = Original;
  std::get<DirectXScalarBindingTy>(Manifest.Arguments[3].Binding).Size = 4;
  expectError(validate(Bytes), error::ErrorCode::INVALID_BINARY,
              "field layout");
}

TEST_F(DirectXShaderTest, CannotUnderstateFeatureRequirements) {
  Manifest.Features = 0;
  expectError(validate(Bytes), error::ErrorCode::INVALID_BINARY, "understates");
  Manifest.Features = uint64_t(dxbc::FeatureFlags::Int64Ops);
  auto Changed = Bytes;
  support::endian::write64le(Changed.data() + part("SFI0") + 8, 0);
  expectError(validate(Changed), error::ErrorCode::INVALID_BINARY,
              "SFI0 understates");
  support::endian::write64le(Changed.data() + part("SFI0") + 8, 1ull << 63);
  expectError(validate(Changed), error::ErrorCode::UNSUPPORTED, "SFI0");
}

TEST_F(DirectXShaderTest, RequiresExecutableDXILFeaturesAndScalarReflection) {
  for (StringRef Name : {"DXIL", "SFI0", "STAT"}) {
    auto Changed = Bytes;
    Changed.replace(part(Name), 4, "PRIV");
    expectError(validate(Changed),
                Name == "STAT" ? error::ErrorCode::UNSUPPORTED
                               : error::ErrorCode::INVALID_BINARY,
                Name == "STAT" ? "STAT" : "required");
  }
  auto Changed = Bytes;
  Changed.replace(part("STAT"), 4, "DXIL");
  expectError(validate(Changed), error::ErrorCode::INVALID_BINARY,
              "more than one DXIL");
  Changed = Bytes;
  Changed.replace(part("STAT"), 4, "RTS0");
  expectError(validate(Changed), error::ErrorCode::UNSUPPORTED,
              "embedded root signatures");
}

TEST_F(DirectXShaderTest, RejectsMalformedContainerAndProgramBounds) {
  for (size_t Size : {size_t(0), size_t(4), size_t(31), Bytes.size() - 1}) {
    SCOPED_TRACE(Size);
    expectError(validate(StringRef(Bytes).take_front(Size)),
                error::ErrorCode::INVALID_BINARY,
                Size < 32 ? "header" : "size");
  }
  auto Changed = Bytes;
  support::endian::write32le(Changed.data() + 28,
                             std::numeric_limits<uint32_t>::max());
  expectError(validate(Changed), error::ErrorCode::INVALID_BINARY,
              "part count");
  Changed = Bytes;
  support::endian::write32le(Changed.data() + 32, 0);
  expectError(validate(Changed), error::ErrorCode::INVALID_BINARY, "part");
  Changed = Bytes;
  support::endian::write32le(Changed.data() + part("DXIL") + 4,
                             std::numeric_limits<uint32_t>::max());
  expectError(validate(Changed), error::ErrorCode::INVALID_BINARY, "part");

  for (size_t Offset : {size_t(16), size_t(20)}) {
    Changed = Bytes;
    support::endian::write32le(Changed.data() + part("DXIL") + 8 + Offset,
                               std::numeric_limits<uint32_t>::max());
    expectError(validate(Changed), error::ErrorCode::INVALID_BINARY, "bounds");
  }
}

TEST_F(DirectXShaderTest, RejectsNonComputeAndUnreadableBitcode) {
  auto Changed = Bytes;
  support::endian::write16le(Changed.data() + part("DXIL") + 10, 0);
  expectError(validate(Changed), error::ErrorCode::UNSUPPORTED, "compute");
  Changed = Bytes;
  Changed[part("DXIL") + 8] = 0x61;
  expectError(validate(Changed), error::ErrorCode::INVALID_BINARY,
              "shader model");
  Changed = Bytes;
  uint32_t Offset =
      support::endian::read32le(Changed.data() + part("DXIL") + 24);
  Changed[part("DXIL") + 16 + Offset] = 0;
  expectError(validate(Changed), error::ErrorCode::INVALID_BINARY, "bitcode");
}

TEST_F(DirectXShaderTest, RejectsMalformedAndUnsupportedModuleMetadata) {
  auto Changed = rewriteProgram([](Module &M) {
    M.eraseNamedMetadata(M.getNamedMetadata("dx.shaderModel"));
  });
  expectError(validate(Changed), error::ErrorCode::INVALID_BINARY,
              "dx.shaderModel");
  Changed =
      rewriteProgram([](Module &M) { M.getFunction("main")->deleteBody(); });
  expectError(validate(Changed), error::ErrorCode::INVALID_BINARY,
              "entry point");
  Changed = rewriteProgram([](Module &M) {
    M.getNamedMetadata("dx.entryPoints")
        ->getOperand(0)
        ->replaceOperandWith(4, MDString::get(M.getContext(), "not a tuple"));
  });
  expectError(validate(Changed), error::ErrorCode::INVALID_BINARY,
              "entry properties");
  Changed = rewriteProgram([](Module &M) {
    auto *Properties = cast<MDNode>(
        M.getNamedMetadata("dx.entryPoints")->getOperand(0)->getOperand(4));
    Properties->replaceOperandWith(
        0, ConstantAsMetadata::get(
               ConstantInt::get(Type::getInt32Ty(M.getContext()), 99)));
  });
  expectError(validate(Changed), error::ErrorCode::UNSUPPORTED,
              "entry property");
}

TEST_F(DirectXShaderTest, RejectsResourceArraysAndNonRawResources) {
  for (auto [Field, Value, Message] :
       {std::tuple{5u, 2u, "resource arrays"},
        std::tuple{6u, unsigned(dxil::ResourceKind::TypedBuffer),
                   "only raw"}}) {
    auto Changed = rewriteProgram([Field = Field, Value = Value](Module &M) {
      auto *List = cast<MDNode>(
          M.getNamedMetadata("dx.resources")->getOperand(0)->getOperand(0));
      auto *Resource = cast<MDNode>(List->getOperand(0));
      Resource->replaceOperandWith(
          Field, ConstantAsMetadata::get(ConstantInt::get(
                     Type::getInt32Ty(M.getContext()), Value)));
    });
    expectError(validate(Changed), error::ErrorCode::UNSUPPORTED, Message);
  }
}

TEST_F(DirectXShaderTest, RejectsAbsentOrMalformedConstantAnnotations) {
  auto Changed = rewriteProgram(
      [](Module &M) {
        M.eraseNamedMetadata(M.getNamedMetadata("dx.typeAnnotations"));
      },
      "STAT");
  expectError(validate(Changed), error::ErrorCode::UNSUPPORTED,
              "retained cbuffer reflection");
  Changed = rewriteProgram(
      [](Module &M) {
        auto *Annotation =
            M.getNamedMetadata("dx.typeAnnotations")->getOperand(0);
        auto *Layout = cast<MDNode>(Annotation->getOperand(2));
        auto *Field = cast<MDNode>(Layout->getOperand(1));
        Field->replaceOperandWith(3, MDString::get(M.getContext(), "not i32"));
      },
      "STAT");
  expectError(validate(Changed), error::ErrorCode::INVALID_BINARY,
              "i32 metadata");
}

TEST_F(DirectXShaderTest, RejectsGlobalConstructorsAndDestructors) {
  for (bool Destructor : {false, true}) {
    auto Changed = rewriteProgram([Destructor](Module &M) {
      if (Destructor)
        appendToGlobalDtors(M, M.getFunction("main"), 65535);
      else
        appendToGlobalCtors(M, M.getFunction("main"), 65535);
    });
    expectError(validate(Changed), error::ErrorCode::UNSUPPORTED,
                "global constructors or destructors");
  }
}

TEST_F(DirectXShaderTest, RejectsUnreflectedGroupSharedStorage) {
  auto Changed = rewriteProgram([](Module &M) {
    auto *Ty = Type::getInt32Ty(M.getContext());
    new GlobalVariable(M, Ty, false, GlobalValue::InternalLinkage,
                       UndefValue::get(Ty), "shared", nullptr,
                       GlobalVariable::NotThreadLocal, 3);
  });
  expectError(validate(Changed), error::ErrorCode::UNSUPPORTED,
              "groupshared storage");
}

} // namespace
