//===- RootSignatureTest.cpp - DirectX launch root layout tests
//------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXRootSignature.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::omp::target::plugin::directx;

namespace {

TEST(DirectXRootSignatureTest, EmptyLayout) {
  withRootSignatureDesc({}, [](const D3D12_ROOT_SIGNATURE_DESC &Desc) {
    EXPECT_EQ(Desc.NumParameters, 0u);
    EXPECT_EQ(Desc.pParameters, nullptr);
    EXPECT_EQ(Desc.NumStaticSamplers, 0u);
    EXPECT_EQ(Desc.pStaticSamplers, nullptr);
    EXPECT_EQ(Desc.Flags, D3D12_ROOT_SIGNATURE_FLAG_NONE);
  });
}

TEST(DirectXRootSignatureTest, BufferTableUsesExplicitRanges) {
  DirectXManifestTy Manifest;
  Manifest.Arguments = {
      {0, DirectXBufferBindingTy{DirectXBufferBindingTy::Kind::UAV, 7, 2, 4}},
      {1, DirectXBufferBindingTy{DirectXBufferBindingTy::Kind::SRV, 3, 5, 4}}};
  withRootSignatureDesc(Manifest, [](const D3D12_ROOT_SIGNATURE_DESC &Desc) {
    ASSERT_EQ(Desc.NumParameters, 1u);
    ASSERT_NE(Desc.pParameters, nullptr);
    const auto &Table = Desc.pParameters[0];
    EXPECT_EQ(Table.ParameterType, D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
    EXPECT_EQ(Table.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
    ASSERT_EQ(Table.DescriptorTable.NumDescriptorRanges, 2u);
    ASSERT_NE(Table.DescriptorTable.pDescriptorRanges, nullptr);
    const auto *Ranges = Table.DescriptorTable.pDescriptorRanges;
    EXPECT_EQ(Ranges[0].RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
    EXPECT_EQ(Ranges[0].BaseShaderRegister, 7u);
    EXPECT_EQ(Ranges[0].RegisterSpace, 2u);
    EXPECT_EQ(Ranges[0].NumDescriptors, 1u);
    EXPECT_EQ(Ranges[0].OffsetInDescriptorsFromTableStart, 0u);
    EXPECT_EQ(Ranges[1].RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
    EXPECT_EQ(Ranges[1].BaseShaderRegister, 3u);
    EXPECT_EQ(Ranges[1].RegisterSpace, 5u);
    EXPECT_EQ(Ranges[1].NumDescriptors, 1u);
    EXPECT_EQ(Ranges[1].OffsetInDescriptorsFromTableStart, 1u);
  });
}

TEST(DirectXRootSignatureTest, ScalarOnlyUsesRootZeroAndKeepsPadding) {
  DirectXManifestTy Manifest;
  Manifest.Constants = DirectXConstantBindingTy{4, 2, 252};
  Manifest.Arguments = {{0, DirectXScalarBindingTy{4, 0}}};
  withRootSignatureDesc(Manifest, [](const D3D12_ROOT_SIGNATURE_DESC &Desc) {
    ASSERT_EQ(Desc.NumParameters, 1u);
    ASSERT_NE(Desc.pParameters, nullptr);
    const auto &Constants = Desc.pParameters[0];
    EXPECT_EQ(Constants.ParameterType,
              D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS);
    EXPECT_EQ(Constants.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
    EXPECT_EQ(Constants.Constants.ShaderRegister, 4u);
    EXPECT_EQ(Constants.Constants.RegisterSpace, 2u);
    EXPECT_EQ(Constants.Constants.Num32BitValues, 63u);
  });
}

TEST(DirectXRootSignatureTest,
     InterleavedArgumentsUseCanonicalOrderAndFullBudget) {
  auto Manifest = DirectXManifestTy::parse(R"({
    "version":1,"entry":"main","shader_model":[6,0],"numthreads":[1,1,1],
    "features":[],"constants":{"register":4,"space":2,"size":252},
    "arguments":[
      {"index":3,"kind":"srv","register":2,"space":5,"min_size":4},
      {"index":2,"kind":"scalar","size":8,"offset":8},
      {"index":1,"kind":"uav","register":3,"space":1,"min_size":4},
      {"index":0,"kind":"scalar","size":4,"offset":0}
    ]
  })");
  ASSERT_TRUE(static_cast<bool>(Manifest)) << toString(Manifest.takeError());
  withRootSignatureDesc(*Manifest, [](const D3D12_ROOT_SIGNATURE_DESC &Desc) {
    ASSERT_EQ(Desc.NumParameters, 2u);
    ASSERT_NE(Desc.pParameters, nullptr);
    const auto &Table = Desc.pParameters[0];
    EXPECT_EQ(Table.ParameterType, D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);
    EXPECT_EQ(Table.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
    ASSERT_EQ(Table.DescriptorTable.NumDescriptorRanges, 2u);
    ASSERT_NE(Table.DescriptorTable.pDescriptorRanges, nullptr);
    const auto *Ranges = Table.DescriptorTable.pDescriptorRanges;
    EXPECT_EQ(Ranges[0].RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_UAV);
    EXPECT_EQ(Ranges[0].BaseShaderRegister, 3u);
    EXPECT_EQ(Ranges[0].RegisterSpace, 1u);
    EXPECT_EQ(Ranges[0].NumDescriptors, 1u);
    EXPECT_EQ(Ranges[0].OffsetInDescriptorsFromTableStart, 0u);
    EXPECT_EQ(Ranges[1].RangeType, D3D12_DESCRIPTOR_RANGE_TYPE_SRV);
    EXPECT_EQ(Ranges[1].BaseShaderRegister, 2u);
    EXPECT_EQ(Ranges[1].RegisterSpace, 5u);
    EXPECT_EQ(Ranges[1].NumDescriptors, 1u);
    EXPECT_EQ(Ranges[1].OffsetInDescriptorsFromTableStart, 1u);
    const auto &Constants = Desc.pParameters[1];
    EXPECT_EQ(Constants.ParameterType,
              D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS);
    EXPECT_EQ(Constants.ShaderVisibility, D3D12_SHADER_VISIBILITY_ALL);
    EXPECT_EQ(Constants.Constants.ShaderRegister, 4u);
    EXPECT_EQ(Constants.Constants.RegisterSpace, 2u);
    EXPECT_EQ(Constants.Constants.Num32BitValues + 1, 64u);
    EXPECT_EQ(Desc.NumStaticSamplers, 0u);
    EXPECT_EQ(Desc.pStaticSamplers, nullptr);
    EXPECT_EQ(Desc.Flags, D3D12_ROOT_SIGNATURE_FLAG_NONE);
  });
}

} // namespace
