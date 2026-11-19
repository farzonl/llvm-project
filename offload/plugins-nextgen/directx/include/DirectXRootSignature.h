//===- DirectXRootSignature.h - DirectX launch root layout --------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef OMPTARGET_PLUGINS_DIRECTX_ROOT_SIGNATURE_H
#define OMPTARGET_PLUGINS_DIRECTX_ROOT_SIGNATURE_H

#include "DirectXManifest.h"
#include "DirectXQueue.h"
#include "llvm/ADT/STLFunctionalExtras.h"

namespace llvm::omp::target::plugin::directx {

// The descriptor and its pointed-to arrays are valid only during Use.
void withRootSignatureDesc(
    const DirectXManifestTy &Manifest,
    function_ref<void(const D3D12_ROOT_SIGNATURE_DESC &)> Use);

// Expected's storage access invokes WRL ComPtr's releasing operator&.
Error createRootSignature(
    const DirectXManifestTy &Manifest, ID3D12Device &Device,
    PFN_D3D12_SERIALIZE_ROOT_SIGNATURE Serialize,
    Microsoft::WRL::ComPtr<ID3D12RootSignature> &RootSignature);

} // namespace llvm::omp::target::plugin::directx

#endif
