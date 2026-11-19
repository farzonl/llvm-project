//===- DirectXRootSignature.cpp - DirectX launch root layout
//---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXRootSignature.h"

namespace llvm::omp::target::plugin::directx {

void withRootSignatureDesc(
    const DirectXManifestTy &Manifest,
    function_ref<void(const D3D12_ROOT_SIGNATURE_DESC &)> Use) {
  SmallVector<D3D12_DESCRIPTOR_RANGE> Ranges;
  for (const auto &Arg : Manifest.Arguments)
    if (const auto *Buffer =
            std::get_if<DirectXBufferBindingTy>(&Arg.Binding)) {
      D3D12_DESCRIPTOR_RANGE Range{};
      Range.RangeType = Buffer->Access == DirectXBufferBindingTy::Kind::SRV
                            ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
                            : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
      Range.NumDescriptors = 1;
      Range.BaseShaderRegister = Buffer->Register;
      Range.RegisterSpace = Buffer->Space;
      Range.OffsetInDescriptorsFromTableStart =
          static_cast<UINT>(Ranges.size());
      Ranges.push_back(Range);
    }

  SmallVector<D3D12_ROOT_PARAMETER, 2> Parameters;
  if (!Ranges.empty()) {
    D3D12_ROOT_PARAMETER Table{};
    Table.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Table.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    Table.DescriptorTable.NumDescriptorRanges =
        static_cast<UINT>(Ranges.size());
    Table.DescriptorTable.pDescriptorRanges = Ranges.data();
    Parameters.push_back(Table);
  }
  if (Manifest.Constants) {
    D3D12_ROOT_PARAMETER Constants{};
    Constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    Constants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    Constants.Constants.ShaderRegister = Manifest.Constants->Register;
    Constants.Constants.RegisterSpace = Manifest.Constants->Space;
    Constants.Constants.Num32BitValues = Manifest.Constants->Size / 4;
    Parameters.push_back(Constants);
  }
  D3D12_ROOT_SIGNATURE_DESC Desc{};
  Desc.NumParameters = static_cast<UINT>(Parameters.size());
  Desc.pParameters = Parameters.empty() ? nullptr : Parameters.data();
  Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  Use(Desc);
}

Error createRootSignature(
    const DirectXManifestTy &Manifest, ID3D12Device &Device,
    PFN_D3D12_SERIALIZE_ROOT_SIGNATURE Serialize,
    Microsoft::WRL::ComPtr<ID3D12RootSignature> &RootSignature) {
  if (!Serialize)
    return error::createOffloadError(error::ErrorCode::BACKEND_FAILURE,
                                     "missing D3D12SerializeRootSignature");
  Microsoft::WRL::ComPtr<ID3D12RootSignature> Root;
  Error Result = Error::success();
  withRootSignatureDesc(Manifest, [&](const D3D12_ROOT_SIGNATURE_DESC &Desc) {
    ErrorAsOutParameter Out(Result);
    Microsoft::WRL::ComPtr<ID3DBlob> Blob, Diagnostics;
    HRESULT Status =
        Serialize(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Blob, &Diagnostics);
    if (FAILED(Status)) {
      std::string Message;
      if (Diagnostics)
        Message.assign(
            static_cast<const char *>(Diagnostics->GetBufferPointer()),
            Diagnostics->GetBufferSize());
      Result = error::createOffloadError(
          error::ErrorCode::BACKEND_FAILURE,
          "D3D12SerializeRootSignature failed (HRESULT 0x%08x): %s",
          static_cast<uint32_t>(Status), Message.c_str());
      return;
    }
    if (!Blob || !Blob->GetBufferPointer() || !Blob->GetBufferSize()) {
      Result =
          error::createOffloadError(error::ErrorCode::BACKEND_FAILURE,
                                    "root serializer returned no bytecode");
      return;
    }
    Result = check(Device.CreateRootSignature(0, Blob->GetBufferPointer(),
                                              Blob->GetBufferSize(),
                                              IID_PPV_ARGS(&Root)),
                   "ID3D12Device::CreateRootSignature");
  });
  if (Result)
    return Result;
  if (!Root)
    return error::createOffloadError(error::ErrorCode::BACKEND_FAILURE,
                                     "device returned no root signature");
  RootSignature = std::move(Root);
  return Error::success();
}

} // namespace llvm::omp::target::plugin::directx
