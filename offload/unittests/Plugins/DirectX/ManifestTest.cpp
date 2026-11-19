//===- ManifestTest.cpp - DirectX launch manifest tests
//---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXManifest.h"
#include "OffloadError.h"

#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <limits>
#include <utility>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::omp::target::plugin::directx;

namespace {

json::Object emptyManifest() {
  return json::Object{{"version", 1},
                      {"entry", "main"},
                      {"shader_model", json::Array{6, 0}},
                      {"numthreads", json::Array{1, 1, 1}},
                      {"features", json::Array{}},
                      {"constants", nullptr},
                      {"arguments", json::Array{}}};
}

std::string encode(json::Object Object) {
  std::string Text;
  raw_string_ostream OS(Text);
  OS << json::Value(std::move(Object));
  return Text;
}

json::Object buffer(unsigned Index, StringRef Kind, unsigned Register = 0,
                    unsigned Space = 0) {
  return json::Object{{"index", Index},
                      {"kind", Kind},
                      {"register", Register},
                      {"space", Space},
                      {"min_size", 4}};
}

json::Object scalar(unsigned Index, unsigned Size, unsigned Offset) {
  return json::Object{
      {"index", Index}, {"kind", "scalar"}, {"size", Size}, {"offset", Offset}};
}

json::Object constants(unsigned Size) {
  return json::Object{{"register", 3}, {"space", 2}, {"size", Size}};
}

void expectError(Expected<DirectXManifestTy> Manifest, error::ErrorCode Code,
                 StringRef Message) {
  ASSERT_FALSE(static_cast<bool>(Manifest));
  handleAllErrors(Manifest.takeError(), [&](const error::OffloadError &Err) {
    EXPECT_EQ(Err.convertToErrorCode(), error::make_error_code(Code));
    EXPECT_TRUE(StringRef(Err.getMessage()).contains(Message))
        << Err.getMessage();
  });
}

Expected<DirectXManifestTy> parseImage(OffloadBinary::OffloadingImage &Image) {
  auto Bytes = OffloadBinary::write(ArrayRef(&Image, 1));
  auto Storage = MemoryBuffer::getMemBufferCopy(Bytes);
  auto Entries = OffloadBinary::create(Storage->getMemBufferRef());
  if (!Entries)
    return Entries.takeError();
  return DirectXManifestTy::parse(*Entries->front());
}

TEST(DirectXManifestTest, DecodesAndCanonicalizesBindings) {
  auto Object = emptyManifest();
  Object["entry"] = "saxpy";
  Object["numthreads"] = json::Array{64, 1, 1};
  Object["features"] = json::Array{"Doubles", "WaveOps"};
  Object["constants"] = constants(16);
  Object["arguments"] = json::Array{buffer(1, "uav", 0, 1), scalar(3, 8, 8),
                                    buffer(0, "srv", 0, 1), scalar(2, 4, 0)};
  std::string Text = encode(std::move(Object));
  auto Manifest = DirectXManifestTy::parse(Text);
  ASSERT_TRUE(static_cast<bool>(Manifest)) << toString(Manifest.takeError());
  Text.assign(Text.size(), 'x');
  EXPECT_EQ(Manifest->Entry, "saxpy");
  EXPECT_EQ(Manifest->ShaderModel, (std::array<uint32_t, 2>{6, 0}));
  EXPECT_EQ(Manifest->NumThreads, (std::array<uint32_t, 3>{64, 1, 1}));
  EXPECT_EQ(Manifest->Features,
            static_cast<uint64_t>(dxbc::FeatureFlags::Doubles) |
                static_cast<uint64_t>(dxbc::FeatureFlags::WaveOps));
  ASSERT_TRUE(Manifest->Constants.has_value());
  EXPECT_EQ(Manifest->Constants->Register, 3u);
  EXPECT_EQ(Manifest->Constants->Space, 2u);
  EXPECT_EQ(Manifest->Constants->Size, 16u);
  ASSERT_EQ(Manifest->Arguments.size(), 4u);
  auto *SRV =
      std::get_if<DirectXBufferBindingTy>(&Manifest->Arguments[0].Binding);
  auto *UAV =
      std::get_if<DirectXBufferBindingTy>(&Manifest->Arguments[1].Binding);
  ASSERT_NE(SRV, nullptr);
  ASSERT_NE(UAV, nullptr);
  EXPECT_EQ(SRV->Access, DirectXBufferBindingTy::Kind::SRV);
  EXPECT_EQ(UAV->Access, DirectXBufferBindingTy::Kind::UAV);
  EXPECT_EQ(SRV->Register, 0u);
  EXPECT_EQ(SRV->Space, 1u);
  EXPECT_EQ(SRV->MinSize, 4u);
  auto *Scalar =
      std::get_if<DirectXScalarBindingTy>(&Manifest->Arguments[3].Binding);
  ASSERT_NE(Scalar, nullptr);
  EXPECT_EQ(Manifest->Arguments[3].Index, 3u);
  EXPECT_EQ(Scalar->Size, 8u);
  EXPECT_EQ(Scalar->Offset, 8u);
}

TEST(DirectXManifestTest, AcceptsEmptyKernelsAndFullRootBudget) {
  auto Empty = DirectXManifestTy::parse(encode(emptyManifest()));
  ASSERT_TRUE(static_cast<bool>(Empty)) << toString(Empty.takeError());
  EXPECT_TRUE(Empty->Arguments.empty());
  EXPECT_FALSE(Empty->Constants.has_value());

  auto Object = emptyManifest();
  Object["numthreads"] = json::Array{1024, 1, 1};
  Object["constants"] = constants(252);
  json::Array Arguments{buffer(0, "srv")};
  for (unsigned I = 0; I < 63; ++I)
    Arguments.push_back(scalar(I + 1, 4, 4 * I));
  Object["arguments"] = std::move(Arguments);
  auto Manifest = DirectXManifestTy::parse(encode(std::move(Object)));
  ASSERT_TRUE(static_cast<bool>(Manifest)) << toString(Manifest.takeError());
  ASSERT_EQ(Manifest->Arguments.size(), 64u);
  auto *Last =
      std::get_if<DirectXScalarBindingTy>(&Manifest->Arguments.back().Binding);
  ASSERT_NE(Last, nullptr);
  EXPECT_EQ(Last->Offset + Last->Size, 252u);
}

TEST(DirectXManifestTest, RejectsMalformedAndUnsupportedHeaders) {
  for (StringRef Text : {"", "{", "[]", "{}", R"({"version":1.0})",
                         R"({"version":-1})", R"({"version":4294967296})"}) {
    SCOPED_TRACE(Text.str());
    expectError(DirectXManifestTy::parse(Text),
                error::ErrorCode::INVALID_BINARY, "manifest");
  }
  auto Object = emptyManifest();
  Object["version"] = 2;
  Object["future_field"] = true;
  expectError(DirectXManifestTy::parse(encode(std::move(Object))),
              error::ErrorCode::UNSUPPORTED, "version 2");
  Object = emptyManifest();
  Object["unknown"] = true;
  expectError(DirectXManifestTy::parse(encode(std::move(Object))),
              error::ErrorCode::INVALID_BINARY, "unknown field");
  for (StringRef Name : {"", "with space", "0main", "main.name", "main\n"}) {
    Object = emptyManifest();
    Object["entry"] = Name;
    expectError(DirectXManifestTy::parse(encode(std::move(Object))),
                error::ErrorCode::INVALID_BINARY, "entry");
  }
  Object = emptyManifest();
  Object["entry"] = StringRef("main\0other", 10);
  expectError(DirectXManifestTy::parse(encode(std::move(Object))),
              error::ErrorCode::INVALID_BINARY, "entry");
}

TEST(DirectXManifestTest, RejectsInvalidGeometryAndFeatures) {
  for (const auto &Threads :
       {json::Array{0, 1, 1}, json::Array{1, 1, 65}, json::Array{1024, 2, 1},
        json::Array{1, 1025, 1}, json::Array{1, 1}, json::Array{1.5, 1, 1},
        json::Array{4294967295u, 4294967295u, 4294967295u}}) {
    auto Object = emptyManifest();
    Object["numthreads"] = json::Array(Threads);
    expectError(DirectXManifestTy::parse(encode(std::move(Object))),
                error::ErrorCode::INVALID_BINARY, "numthreads");
  }
  for (const auto &Model :
       {json::Array{5, 1}, json::Array{6, 16}, json::Array{7, 0}}) {
    auto Object = emptyManifest();
    Object["shader_model"] = json::Array(Model);
    expectError(DirectXManifestTy::parse(encode(std::move(Object))),
                error::ErrorCode::UNSUPPORTED, "shader model");
  }
  for (StringRef Feature : {"unknown", "RESERVED", "NextUnusedBit"}) {
    auto Object = emptyManifest();
    Object["features"] = json::Array{Feature};
    expectError(DirectXManifestTy::parse(encode(std::move(Object))),
                error::ErrorCode::UNSUPPORTED, "feature");
  }
  auto Object = emptyManifest();
  Object["features"] = json::Array{"WaveOps", "WaveOps"};
  expectError(DirectXManifestTy::parse(encode(std::move(Object))),
              error::ErrorCode::INVALID_BINARY, "duplicate feature");
  Object = emptyManifest();
  Object["features"] = json::Array{true};
  expectError(DirectXManifestTy::parse(encode(std::move(Object))),
              error::ErrorCode::INVALID_BINARY, "strings");
}

TEST(DirectXManifestTest, ValidatesRegisterNamespacesAndBufferRanges) {
  auto Object = emptyManifest();
  Object["arguments"] =
      json::Array{buffer(0, "srv"), buffer(1, "uav"), buffer(2, "srv", 0, 1)};
  auto Manifest = DirectXManifestTy::parse(encode(std::move(Object)));
  ASSERT_TRUE(static_cast<bool>(Manifest)) << toString(Manifest.takeError());
  Object = emptyManifest();
  Object["arguments"] = json::Array{buffer(0, "srv"), buffer(1, "srv")};
  expectError(DirectXManifestTy::parse(encode(std::move(Object))),
              error::ErrorCode::INVALID_BINARY, "duplicates a buffer register");
  for (auto [Field, Number] : {std::pair<StringRef, uint64_t>{"min_size", 0},
                               {"min_size", 3},
                               {"register", 4294967295u},
                               {"space", 0xfffffff0u},
                               {"register", uint64_t(1) << 32}}) {
    Object = emptyManifest();
    auto Buffer = buffer(0, "srv");
    Buffer[Field] = Number;
    Object["arguments"] = json::Array{std::move(Buffer)};
    expectError(DirectXManifestTy::parse(encode(std::move(Object))),
                error::ErrorCode::INVALID_BINARY, Field);
  }
  Object = emptyManifest();
  auto Buffer = buffer(0, "srv");
  Buffer["offset"] = 0;
  Object["arguments"] = json::Array{std::move(Buffer)};
  expectError(DirectXManifestTy::parse(encode(std::move(Object))),
              error::ErrorCode::INVALID_BINARY, "unknown field");
}

TEST(DirectXManifestTest, RejectsInvalidScalarLayoutsAndIndices) {
  for (unsigned Size : {0u, 3u, 256u, std::numeric_limits<unsigned>::max()}) {
    auto Object = emptyManifest();
    Object["constants"] = constants(Size);
    Object["arguments"] = json::Array{scalar(0, 4, 0)};
    expectError(DirectXManifestTy::parse(encode(std::move(Object))),
                error::ErrorCode::INVALID_BINARY, "constants.size");
  }
  for (const auto &Arguments :
       {json::Array{scalar(0, 0, 0)}, json::Array{scalar(0, 16, 0)},
        json::Array{scalar(0, 8, 4)}, json::Array{scalar(0, 4, 16)},
        json::Array{scalar(0, 4, 4294967292u)},
        json::Array{scalar(0, 8, 0), scalar(1, 4, 4)},
        json::Array{scalar(0, 4, 0), scalar(0, 4, 8)},
        json::Array{scalar(1, 4, 0)}}) {
    auto Object = emptyManifest();
    Object["constants"] = constants(16);
    Object["arguments"] = json::Array(Arguments);
    expectError(DirectXManifestTy::parse(encode(std::move(Object))),
                error::ErrorCode::INVALID_BINARY, "argument");
  }
  auto Object = emptyManifest();
  Object["arguments"] = json::Array{scalar(0, 4, 0)};
  expectError(DirectXManifestTy::parse(encode(std::move(Object))),
              error::ErrorCode::INVALID_BINARY,
              "outside the root constant block");
  Object = emptyManifest();
  Object["constants"] = constants(4);
  expectError(DirectXManifestTy::parse(encode(std::move(Object))),
              error::ErrorCode::INVALID_BINARY, "requires scalar arguments");
  Object = emptyManifest();
  Object["arguments"] =
      json::Array{json::Object{{"index", 0}, {"kind", "texture"}}};
  expectError(DirectXManifestTy::parse(encode(std::move(Object))),
              error::ErrorCode::UNSUPPORTED, "argument kind");
}

TEST(DirectXManifestTest, PreservesPerImageManifestAndPayload) {
  SmallVector<OffloadBinary::OffloadingImage> Images;
  std::string JSON[2];
  StringRef Names[] = {"first", "second"};
  StringRef Payloads[] = {StringRef("DXBC\0one", 8), StringRef("DXBC\0two", 8)};
  for (unsigned I = 0; I < 2; ++I) {
    auto Object = emptyManifest();
    Object["entry"] = Names[I];
    JSON[I] = encode(std::move(Object));
    OffloadBinary::OffloadingImage Image;
    Image.TheImageKind = IMG_DXIL;
    Image.StringData[DirectXManifestTy::MetadataKey] = JSON[I];
    Image.Image = MemoryBuffer::getMemBufferCopy(Payloads[I]);
    Images.push_back(std::move(Image));
  }
  auto Bytes = OffloadBinary::write(Images);
  auto Storage = MemoryBuffer::getMemBufferCopy(Bytes);
  auto Entries = OffloadBinary::create(Storage->getMemBufferRef());
  ASSERT_TRUE(static_cast<bool>(Entries)) << toString(Entries.takeError());
  ASSERT_EQ(Entries->size(), 2u);
  for (unsigned I = 0; I < 2; ++I) {
    EXPECT_EQ((*Entries)[I]->getImage(), Payloads[I]);
    auto Manifest = DirectXManifestTy::parse(*(*Entries)[I]);
    ASSERT_TRUE(static_cast<bool>(Manifest)) << toString(Manifest.takeError());
    EXPECT_EQ(Manifest->Entry, Names[I]);
  }
}

TEST(DirectXManifestTest, RejectsInvalidPackageEntries) {
  std::string JSON = encode(emptyManifest());
  OffloadBinary::OffloadingImage Image;
  Image.TheImageKind = IMG_DXIL;
  Image.Image = MemoryBuffer::getMemBufferCopy("DXBC");
  expectError(parseImage(Image), error::ErrorCode::INVALID_BINARY,
              "missing directx.manifest");
  Image.StringData[DirectXManifestTy::MetadataKey] = JSON;
  Image.TheImageKind = IMG_SPIRV;
  expectError(parseImage(Image), error::ErrorCode::INVALID_BINARY,
              "DXIL entry");
  Image.TheImageKind = IMG_DXIL;
  Image.TheOffloadKind = OFK_OpenMP;
  expectError(parseImage(Image), error::ErrorCode::INVALID_BINARY,
              "producer kind");
  Image.TheOffloadKind = OFK_None;
  Image.Flags = OIF_Metadata;
  expectError(parseImage(Image), error::ErrorCode::INVALID_BINARY, "flags");
  Image.Flags = 0;
  Image.Image = MemoryBuffer::getMemBufferCopy("");
  expectError(parseImage(Image), error::ErrorCode::INVALID_BINARY, "nonempty");
}

} // namespace
