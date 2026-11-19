//===- DirectXFeatures.h - DirectX device compatibility -----------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef OMPTARGET_PLUGINS_DIRECTX_FEATURES_H
#define OMPTARGET_PLUGINS_DIRECTX_FEATURES_H

#include "DirectXManifest.h"
#include "DirectXQueue.h"
#include "llvm/ADT/STLFunctionalExtras.h"

namespace llvm::omp::target::plugin::directx {

Error checkDeviceCapabilities(
    const DirectXManifestTy &Manifest,
    function_ref<HRESULT(D3D12_FEATURE, void *, UINT)> Query);

inline Error checkDeviceCapabilities(const DirectXManifestTy &Manifest,
                                     ID3D12Device &Device) {
  return checkDeviceCapabilities(
      Manifest, [&](D3D12_FEATURE Feature, void *Data, UINT Size) {
        return Device.CheckFeatureSupport(Feature, Data, Size);
      });
}

} // namespace llvm::omp::target::plugin::directx

#endif
