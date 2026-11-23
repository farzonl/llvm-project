//===- WorkgroupTest.cpp - DirectX workgroup recipe tests
//-------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeWriter.h"

#include "DirectXWorkgroup.h"
#include "OffloadError.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/Base64.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::omp::target::plugin::directx;

namespace {

constexpr StringLiteral Recipe = R"(
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { ptr }
define void @main(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  ret void
}
attributes #0 = {
  "dx.kernel" "dx.workgroup.specializable" "hlsl.numthreads"="1,1,1"
}
!0 = !{i32 8, i32 8, !1}
!1 = !{i32 0, i32 8}
)";

std::string recipeBitcode(StringRef IR = Recipe) {
  LLVMContext Context;
  SMDiagnostic Diagnostic;
  auto Module = parseAssemblyString(IR, Diagnostic, Context);
  EXPECT_TRUE(static_cast<bool>(Module));
  if (!Module)
    return {};
  std::string Bytes;
  raw_string_ostream OS(Bytes);
  WriteBitcodeToFile(*Module, OS);
  return Bytes;
}

DirectXManifestTy manifest() {
  DirectXManifestTy Manifest;
  Manifest.Entry = "main";
  Manifest.ShaderModel = {6, 0};
  Manifest.NumThreads = {1, 1, 1};
  Manifest.Capture = DirectXCaptureLayoutTy{8, {0}};
  Manifest.Arguments.push_back({0, DirectXBufferBindingTy{}});
  return Manifest;
}

Expected<std::unique_ptr<MemoryBuffer>>
readRecipe(std::optional<StringRef> Encoded, const DirectXManifestTy &Manifest,
           OffloadKind Kind = OFK_SYCL) {
  OffloadBinary::OffloadingImage Image;
  Image.TheImageKind = IMG_DXIL;
  Image.TheOffloadKind = Kind;
  Image.Image = MemoryBuffer::getMemBuffer("");
  if (Encoded)
    Image.StringData[offloading::directx::WorkgroupIRKey] = *Encoded;
  auto Bytes = OffloadBinary::write(ArrayRef(&Image, 1));
  auto Storage = MemoryBuffer::getMemBufferCopy(Bytes);
  auto Entries = OffloadBinary::create(Storage->getMemBufferRef());
  if (!Entries)
    return Entries.takeError();
  return readWorkgroupIR(*Entries->front(), Manifest);
}

void expectError(Expected<std::unique_ptr<MemoryBuffer>> Result,
                 error::ErrorCode Code, StringRef Message) {
  ASSERT_FALSE(static_cast<bool>(Result));
  handleAllErrors(Result.takeError(), [&](const error::OffloadError &Err) {
    EXPECT_EQ(Err.convertToErrorCode(), error::make_error_code(Code));
    EXPECT_TRUE(StringRef(Err.getMessage()).contains(Message))
        << Err.getMessage();
  });
}

TEST(DirectXWorkgroupTest, AbsentRecipeKeepsAOT) {
  auto Result = readRecipe(std::nullopt, manifest());
  ASSERT_TRUE(static_cast<bool>(Result)) << toString(Result.takeError());
  EXPECT_FALSE(*Result);
}

TEST(DirectXWorkgroupTest, ValidRecipeOwnsItsBytes) {
  std::string Bytes = recipeBitcode();
  std::string Encoded = encodeBase64(Bytes);
  auto Result = readRecipe(Encoded, manifest());
  ASSERT_TRUE(static_cast<bool>(Result)) << toString(Result.takeError());
  ASSERT_TRUE(*Result);
  Encoded.assign(Encoded.size(), 'x');
  EXPECT_EQ((*Result)->getBuffer(), Bytes);
}

TEST(DirectXWorkgroupTest, RejectsMalformedEncodingAndBitcode) {
  expectError(readRecipe("", manifest()), error::ErrorCode::INVALID_BINARY,
              "empty recipe");
  expectError(readRecipe("????", manifest()), error::ErrorCode::INVALID_BINARY,
              "Base64");
  expectError(readRecipe(encodeBase64(StringRef("not bitcode")), manifest()),
              error::ErrorCode::INVALID_BINARY, "bitcode");
}

TEST(DirectXWorkgroupTest, MatchesNarrowCaptureSourceWidth) {
  constexpr StringLiteral NarrowRecipe = R"(
target datalayout = "e-p:64:64-i64:64"
target triple = "dxil-pc-shadermodel6.0-compute"
%Capture = type { i16 }
define void @main(ptr byval(%Capture) %args) #0 !dx.capture.layout !0 {
  ret void
}
attributes #0 = {
  "dx.kernel" "dx.workgroup.specializable" "hlsl.numthreads"="1,1,1"
}
!0 = !{i32 2, i32 8, !1}
!1 = !{i32 0, i32 2}
)";
  auto Manifest = manifest();
  Manifest.Capture = DirectXCaptureLayoutTy{2, {0}, {2}};
  Manifest.Constants = DirectXConstantBindingTy{0, 0, 4};
  Manifest.Arguments[0].Binding = DirectXScalarBindingTy{4, 0};
  std::string Encoded = encodeBase64(recipeBitcode(NarrowRecipe));
  auto Result = readRecipe(Encoded, Manifest);
  ASSERT_TRUE(static_cast<bool>(Result)) << toString(Result.takeError());
  Manifest.Capture->Sizes[0] = 1;
  expectError(readRecipe(Encoded, Manifest), error::ErrorCode::INVALID_BINARY,
              "capture field");
}

TEST(DirectXWorkgroupTest, RequiresSYCLCapture) {
  std::string Encoded = encodeBase64(recipeBitcode());
  expectError(readRecipe(Encoded, manifest(), OFK_None),
              error::ErrorCode::INVALID_BINARY, "SYCL capture");
  auto Manifest = manifest();
  Manifest.Capture.reset();
  expectError(readRecipe(Encoded, Manifest), error::ErrorCode::INVALID_BINARY,
              "SYCL capture");
}

TEST(DirectXWorkgroupTest, RejectsMismatchedImageIdentity) {
  std::string Encoded = encodeBase64(recipeBitcode());
  auto Manifest = manifest();
  Manifest.Entry = "other";
  expectError(readRecipe(Encoded, Manifest), error::ErrorCode::INVALID_BINARY,
              "entry point");
  Manifest = manifest();
  Manifest.NumThreads = {2, 1, 1};
  expectError(readRecipe(Encoded, Manifest), error::ErrorCode::INVALID_BINARY,
              "default workgroup");
  Manifest = manifest();
  Manifest.ShaderModel = {6, 1};
  expectError(readRecipe(Encoded, Manifest), error::ErrorCode::INVALID_BINARY,
              "target");
  Manifest = manifest();
  Manifest.Capture->Size = 16;
  expectError(readRecipe(Encoded, Manifest), error::ErrorCode::INVALID_BINARY,
              "capture record");
  Manifest = manifest();
  Manifest.Capture->Offsets[0] = 4;
  expectError(readRecipe(Encoded, Manifest), error::ErrorCode::INVALID_BINARY,
              "capture field");
}

TEST(DirectXWorkgroupTest, RejectsFixedKernelRecipe) {
  std::string IR = Recipe.str();
  std::string Marker = "\"dx.workgroup.specializable\"";
  IR.erase(IR.find(Marker), Marker.size());
  expectError(readRecipe(encodeBase64(recipeBitcode(IR)), manifest()),
              error::ErrorCode::INVALID_BINARY, "entry point");
}

TEST(DirectXWorkgroupTest, RejectsAdditionalKernels) {
  std::string IR = Recipe.str() + "\ndefine void @other() #0 { ret void }\n";
  expectError(readRecipe(encodeBase64(recipeBitcode(IR)), manifest()),
              error::ErrorCode::INVALID_BINARY, "kernel count");
}

TEST(DirectXWorkgroupTest, RejectsInvalidShapesBeforeToolDiscovery) {
  for (const auto &Shape : {std::array<uint32_t, 3>{0, 1, 1},
                            {1025, 1, 1},
                            {1, 1025, 1},
                            {1, 1, 65},
                            {33, 33, 1}})
    expectError(compileWorkgroup("", Shape), error::ErrorCode::INVALID_ARGUMENT,
                "workgroup");
}

TEST(DirectXWorkgroupTest, ReportsFailedCompiler) {
  if (!std::getenv("OFFLOAD_DIRECTX_COMPILER_PATH") ||
      !std::getenv("OFFLOAD_DIRECTX_DXIL_VALIDATOR"))
    GTEST_SKIP() << "DirectX specialization tools are not configured";
  expectError(compileWorkgroup("not bitcode", {3, 1, 1}),
              error::ErrorCode::BACKEND_FAILURE, "failed");
}

} // namespace
