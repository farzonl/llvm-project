//===- FeaturesTest.cpp - DirectX device compatibility tests
//---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXFeatures.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::omp::target::plugin::directx;

namespace {

using F = dxbc::FeatureFlags;

struct FeatureQueries {
  D3D_SHADER_MODEL Model = D3D_SHADER_MODEL_6_0;
  uint64_t Available = 0;
  std::optional<D3D12_FEATURE> FailingQuery;
  HRESULT Failure = E_INVALIDARG;
  unsigned Calls = 0;

  HRESULT operator()(D3D12_FEATURE Feature, void *Data, UINT) {
    ++Calls;
    if (FailingQuery == Feature)
      return Failure;
    switch (Feature) {
    case D3D12_FEATURE_SHADER_MODEL: {
      auto &Query = *static_cast<D3D12_FEATURE_DATA_SHADER_MODEL *>(Data);
      Query.HighestShaderModel = std::min(Model, Query.HighestShaderModel);
      break;
    }
    case D3D12_FEATURE_D3D12_OPTIONS:
      static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS *>(Data)
          ->DoublePrecisionFloatShaderOps =
          (Available & uint64_t(F::Doubles)) != 0;
      break;
    case D3D12_FEATURE_D3D12_OPTIONS1: {
      auto &Query = *static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS1 *>(Data);
      Query.WaveOps = (Available & uint64_t(F::WaveOps)) != 0;
      Query.Int64ShaderOps = (Available & uint64_t(F::Int64Ops)) != 0;
      break;
    }
    case D3D12_FEATURE_D3D12_OPTIONS4:
      static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS4 *>(Data)
          ->Native16BitShaderOpsSupported =
          (Available & uint64_t(F::NativeLowPrecision)) != 0;
      break;
    case D3D12_FEATURE_D3D12_OPTIONS9:
      static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS9 *>(Data)
          ->AtomicInt64OnGroupSharedSupported =
          (Available & uint64_t(F::AtomicInt64OnGroupShared)) != 0;
      break;
    default:
      ADD_FAILURE() << "unexpected feature query";
      return E_INVALIDARG;
    }
    return S_OK;
  }
};

void expectError(Error Err, error::ErrorCode Code, StringRef Message) {
  ASSERT_TRUE(static_cast<bool>(Err));
  handleAllErrors(std::move(Err), [&](const error::OffloadError &Detail) {
    EXPECT_EQ(Detail.convertToErrorCode(), error::make_error_code(Code));
    EXPECT_TRUE(StringRef(Detail.getMessage()).contains(Message))
        << Detail.getMessage();
  });
}

TEST(DirectXFeaturesTest, ChecksOnlyRequiredCapabilities) {
  DirectXManifestTy Manifest;
  Manifest.ShaderModel = {6, 0};
  FeatureQueries Queries;
  Queries.FailingQuery = D3D12_FEATURE_D3D12_OPTIONS1;
  auto Err = checkDeviceCapabilities(Manifest, Queries);
  EXPECT_FALSE(static_cast<bool>(Err)) << toString(std::move(Err));
  EXPECT_EQ(Queries.Calls, 1u);
}

TEST(DirectXFeaturesTest, RejectsMissingShaderModelAndEachOptionalFeature) {
  DirectXManifestTy Manifest;
  Manifest.ShaderModel = {6, 1};
  FeatureQueries Queries;
  expectError(checkDeviceCapabilities(Manifest, Queries),
              error::ErrorCode::UNSUPPORTED, "shader model");
  Manifest.ShaderModel = {6, 0};
  for (auto [Flag, Name] :
       {std::pair{F::Doubles, "Doubles"}, std::pair{F::WaveOps, "WaveOps"},
        std::pair{F::Int64Ops, "Int64Ops"},
        std::pair{F::NativeLowPrecision, "NativeLowPrecision"},
        std::pair{F::AtomicInt64OnGroupShared, "AtomicInt64OnGroupShared"}}) {
    SCOPED_TRACE(Name);
    Manifest.Features = uint64_t(Flag);
    Queries.Available = 0;
    expectError(checkDeviceCapabilities(Manifest, Queries),
                error::ErrorCode::UNSUPPORTED, Name);
    Queries.Available = uint64_t(Flag);
    auto Err = checkDeviceCapabilities(Manifest, Queries);
    EXPECT_FALSE(static_cast<bool>(Err)) << toString(std::move(Err));
  }
}

TEST(DirectXFeaturesTest,
     RejectsUnimplementedFeaturesAndPreservesDriverErrors) {
  DirectXManifestTy Manifest;
  Manifest.ShaderModel = {6, 0};
  Manifest.Features = uint64_t(F::ResourceDescriptorHeapIndexing);
  FeatureQueries Queries;
  expectError(checkDeviceCapabilities(Manifest, Queries),
              error::ErrorCode::UNSUPPORTED, "cannot check");
  EXPECT_EQ(Queries.Calls, 0u);
  Manifest.Features = 0;
  Queries.FailingQuery = D3D12_FEATURE_SHADER_MODEL;
  expectError(checkDeviceCapabilities(Manifest, Queries),
              error::ErrorCode::UNSUPPORTED, "SHADER_MODEL");
  Queries.Failure = DXGI_ERROR_DEVICE_REMOVED;
  expectError(checkDeviceCapabilities(Manifest, Queries),
              error::ErrorCode::BACKEND_FAILURE, "HRESULT");
  Queries.FailingQuery = D3D12_FEATURE_D3D12_OPTIONS4;
  Queries.Failure = E_INVALIDARG;
  Manifest.Features = uint64_t(F::NativeLowPrecision);
  expectError(checkDeviceCapabilities(Manifest, Queries),
              error::ErrorCode::UNSUPPORTED, "D3D12_OPTIONS4");
}

} // namespace
