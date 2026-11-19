//===- DirectXArguments.cpp - DirectX argument bindings
//--------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXArguments.h"
#include "llvm/Support/Endian.h"

#include <cstring>
#include <limits>

namespace llvm::omp::target::plugin::directx {

using error::ErrorCode;
using Microsoft::WRL::ComPtr;

Expected<DirectXRawBufferViewTy> getRawBufferView(uint64_t ResourceOffset,
                                                  uint64_t Remaining,
                                                  uint32_t MinSize) {
  if (ResourceOffset % D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT)
    return error::createOffloadError(
        ErrorCode::INVALID_ARGUMENT,
        "DirectX raw buffer offset must be 16-byte aligned");
  if (Remaining < MinSize || Remaining < 4)
    return error::createOffloadError(
        ErrorCode::INVALID_SIZE,
        "DirectX buffer argument is smaller than its minimum view size");
  uint64_t Elements = Remaining / 4;
  // Keep the prototype within the SDK's portable buffer-view texel limit.
  if (Elements >
      (uint64_t{1} << D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP))
    return error::createOffloadError(
        ErrorCode::UNSUPPORTED,
        "DirectX raw buffer view exceeds the prototype element limit");
  return DirectXRawBufferViewTy{ResourceOffset / 4,
                                static_cast<uint32_t>(Elements)};
}

Error DirectXArgumentBindingsTy::prepare(
    const DirectXManifestTy &Manifest, ArrayRef<void *> Args,
    ArrayRef<int64_t> Sizes,
    function_ref<Expected<DirectXResolvedBufferTy>(const void *)> Resolve) {
  if (Args.size() != Manifest.Arguments.size() || Sizes.size() != Args.size())
    return error::createOffloadError(
        ErrorCode::INVALID_ARGUMENT,
        "DirectX requires one argument value and size per manifest argument");

  DirectXArgumentBindingsTy Result;
  if (Manifest.Constants)
    Result.Constants.resize(Manifest.Constants->Size / 4, 0);
  for (const auto &Arg : Manifest.Arguments) {
    if (!Args[Arg.Index])
      return error::createOffloadError(ErrorCode::INVALID_ARGUMENT,
                                       "DirectX argument %u has no value",
                                       Arg.Index);
    if (const auto *Scalar =
            std::get_if<DirectXScalarBindingTy>(&Arg.Binding)) {
      if (Sizes[Arg.Index] != Scalar->Size)
        return error::createOffloadError(
            ErrorCode::INVALID_SIZE, "DirectX scalar argument %u size mismatch",
            Arg.Index);
      auto *Bytes = static_cast<const char *>(Args[Arg.Index]);
      for (uint32_t I = 0; I < Scalar->Size / 4; ++I)
        Result.Constants[Scalar->Offset / 4 + I] =
            support::endian::read32le(Bytes + I * 4);
      continue;
    }

    if (Sizes[Arg.Index] != static_cast<int64_t>(sizeof(void *)))
      return error::createOffloadError(
          ErrorCode::INVALID_SIZE,
          "DirectX buffer argument %u requires a pointer-sized value",
          Arg.Index);
    const void *Pointer = nullptr;
    std::memcpy(&Pointer, Args[Arg.Index], sizeof(Pointer));
    if (!Pointer)
      return error::createOffloadError(ErrorCode::INVALID_ARGUMENT,
                                       "DirectX buffer argument %u is null",
                                       Arg.Index);
    auto Resolved = Resolve(Pointer);
    if (!Resolved)
      return Resolved.takeError();
    auto &Buffer = Resolved->Buffer;
    if (!Buffer.Resource)
      return error::createOffloadError(ErrorCode::BACKEND_FAILURE,
                                       "DirectX argument has no resource");
    const auto &Binding = std::get<DirectXBufferBindingTy>(Arg.Binding);
    bool IsSRV = Binding.Access == DirectXBufferBindingTy::Kind::SRV;
    auto Desc = Buffer.Resource->GetDesc();
    if (Desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        Buffer.Offset > Desc.Width ||
        Resolved->Size > Desc.Width - Buffer.Offset)
      return error::createOffloadError(
          ErrorCode::BACKEND_FAILURE,
          "DirectX argument allocation exceeds its backing buffer");
    if ((IsSRV && (Desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) ||
        (!IsSRV && !(Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)))
      return error::createOffloadError(
          ErrorCode::INVALID_ARGUMENT,
          "DirectX buffer resource does not support the argument access kind");
    auto View =
        getRawBufferView(Buffer.Offset, Resolved->Size, Binding.MinSize);
    if (!View)
      return View.takeError();
    DirectXBufferArgumentTy Argument;
    Argument.Resource = std::move(Buffer.Resource);
    if (IsSRV) {
      D3D12_SHADER_RESOURCE_VIEW_DESC SRV{};
      SRV.Format = DXGI_FORMAT_R32_TYPELESS;
      SRV.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      SRV.Buffer.FirstElement = View->FirstElement;
      SRV.Buffer.NumElements = View->NumElements;
      SRV.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
      Argument.View = SRV;
    } else {
      D3D12_UNORDERED_ACCESS_VIEW_DESC UAV{};
      UAV.Format = DXGI_FORMAT_R32_TYPELESS;
      UAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      UAV.Buffer.FirstElement = View->FirstElement;
      UAV.Buffer.NumElements = View->NumElements;
      UAV.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
      Argument.View = UAV;
    }
    Result.Buffers.push_back(std::move(Argument));
  }
  *this = std::move(Result);
  return Error::success();
}

Error DirectXArgumentBindingsTy::createDescriptorHeap(ID3D12Device &Device) {
  if (Buffers.empty()) {
    Heap.Reset();
    return Error::success();
  }
  if (Buffers.size() > D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1)
    return error::createOffloadError(ErrorCode::UNSUPPORTED,
                                     "too many DirectX argument descriptors");
  for (const auto &Buffer : Buffers) {
    ComPtr<ID3D12Device> Owner;
    if (auto Err = check(Buffer.Resource->GetDevice(IID_PPV_ARGS(&Owner)),
                         "ID3D12Resource::GetDevice"))
      return Err;
    if (Owner.Get() != &Device)
      return error::createOffloadError(
          ErrorCode::INVALID_DEVICE,
          "DirectX argument resource belongs to another device");
  }
  D3D12_DESCRIPTOR_HEAP_DESC Desc{};
  Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  Desc.NumDescriptors = static_cast<UINT>(Buffers.size());
  Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> NewHeap;
  if (auto Err =
          check(Device.CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&NewHeap)),
                "ID3D12Device::CreateDescriptorHeap"))
    return Err;
  if (!NewHeap)
    return error::createOffloadError(
        ErrorCode::BACKEND_FAILURE,
        "DirectX device returned no descriptor heap");
  auto Handle = NewHeap->GetCPUDescriptorHandleForHeapStart();
  UINT Increment = Device.GetDescriptorHandleIncrementSize(Desc.Type);
  if (!Increment || !Handle.ptr ||
      !NewHeap->GetGPUDescriptorHandleForHeapStart().ptr ||
      Buffers.size() - 1 >
          (std::numeric_limits<SIZE_T>::max() - Handle.ptr) / Increment)
    return error::createOffloadError(ErrorCode::BACKEND_FAILURE,
                                     "invalid DirectX descriptor heap handles");
  for (const auto &Buffer : Buffers) {
    if (const auto *SRV =
            std::get_if<D3D12_SHADER_RESOURCE_VIEW_DESC>(&Buffer.View))
      Device.CreateShaderResourceView(Buffer.Resource.Get(), SRV, Handle);
    else
      Device.CreateUnorderedAccessView(
          Buffer.Resource.Get(), nullptr,
          &std::get<D3D12_UNORDERED_ACCESS_VIEW_DESC>(Buffer.View), Handle);
    Handle.ptr += Increment;
  }
  if (auto Err = check(Device.GetDeviceRemovedReason(),
                       "creating DirectX descriptors"))
    return Err;
  Heap = std::move(NewHeap);
  return Error::success();
}

Error DirectXArgumentBindingsTy::record(
    ID3D12GraphicsCommandList &List, ID3D12RootSignature &RootSignature) const {
  if (!Buffers.empty() && !Heap)
    return error::createOffloadError(
        ErrorCode::INVALID_ARGUMENT,
        "DirectX argument descriptors are not ready");
  List.SetComputeRootSignature(&RootSignature);
  if (!Buffers.empty()) {
    ID3D12DescriptorHeap *Heaps[] = {Heap.Get()};
    List.SetDescriptorHeaps(1, Heaps);
    List.SetComputeRootDescriptorTable(
        0, Heap->GetGPUDescriptorHandleForHeapStart());
  }
  if (!Constants.empty())
    List.SetComputeRoot32BitConstants(Buffers.empty() ? 0 : 1,
                                      static_cast<UINT>(Constants.size()),
                                      Constants.data(), 0);
  return Error::success();
}

} // namespace llvm::omp::target::plugin::directx
