//===- DirectXArguments.h - DirectX argument bindings ------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef OMPTARGET_PLUGINS_DIRECTX_ARGUMENTS_H
#define OMPTARGET_PLUGINS_DIRECTX_ARGUMENTS_H

#include "DirectXManifest.h"
#include "DirectXQueue.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"

#include <cstdint>
#include <variant>

namespace llvm::omp::target::plugin::directx {

struct DirectXRawBufferViewTy {
  uint64_t FirstElement;
  uint32_t NumElements;
};

Expected<DirectXRawBufferViewTy>
getRawBufferView(uint64_t ResourceOffset, uint64_t Remaining, uint32_t MinSize);

struct DirectXResolvedBufferTy {
  DirectXBufferTy Buffer;
  // Remaining live user-allocation bytes, not backing-resource capacity.
  uint64_t Size = 0;
};

struct DirectXBufferArgumentTy {
  Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
  std::variant<D3D12_SHADER_RESOURCE_VIEW_DESC,
               D3D12_UNORDERED_ACCESS_VIEW_DESC>
      View;
};

// Retain this object and the root signature until submitted work completes.
struct DirectXArgumentBindingsTy {
  SmallVector<DirectXBufferArgumentTy> Buffers;
  SmallVector<uint32_t> Constants;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> Heap;

  // Manifest must already be validated. Resolve searches only the launch
  // context's live allocations. Buffer.Offset includes physical alignment
  // padding; Size excludes padding and pool capacity.
  Error prepare(
      const DirectXManifestTy &Manifest, ArrayRef<void *> Args,
      ArrayRef<int64_t> Sizes,
      function_ref<Expected<DirectXResolvedBufferTy>(const void *)> Resolve);

  Error createDescriptorHeap(ID3D12Device &Device);
  Error record(ID3D12GraphicsCommandList &List,
               ID3D12RootSignature &RootSignature) const;
};

} // namespace llvm::omp::target::plugin::directx

#endif
