//===- DirectXFeatures.cpp - DirectX device compatibility
//------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DirectXFeatures.h"
#include "llvm/BinaryFormat/DXContainer.h"

namespace llvm::omp::target::plugin::directx {
namespace {

Error unsupported(const char *Feature) {
  return error::createOffloadError(
      error::ErrorCode::UNSUPPORTED,
      "DirectX device does not support required feature %s", Feature);
}

Error checkQuery(HRESULT Result, const char *Feature) {
  if (Result == E_INVALIDARG)
    return unsupported(Feature);
  return check(Result, Feature);
}

} // namespace

Error checkDeviceCapabilities(
    const DirectXManifestTy &Manifest,
    function_ref<HRESULT(D3D12_FEATURE, void *, UINT)> Query) {
  using F = dxbc::FeatureFlags;
  constexpr uint64_t Supported =
      uint64_t(F::Doubles) | uint64_t(F::WaveOps) | uint64_t(F::Int64Ops) |
      uint64_t(F::NativeLowPrecision) | uint64_t(F::AtomicInt64OnGroupShared);
  if (Manifest.Features & ~Supported)
    return error::createOffloadError(
        error::ErrorCode::UNSUPPORTED,
        "DirectX prototype cannot check required feature mask 0x%llx",
        static_cast<unsigned long long>(Manifest.Features & ~Supported));

  auto Requested = static_cast<D3D_SHADER_MODEL>(
      (Manifest.ShaderModel[0] << 4) | Manifest.ShaderModel[1]);
  D3D12_FEATURE_DATA_SHADER_MODEL Model{Requested};
  if (auto Err =
          checkQuery(Query(D3D12_FEATURE_SHADER_MODEL, &Model, sizeof(Model)),
                     "CheckFeatureSupport(SHADER_MODEL)"))
    return Err;
  if (Model.HighestShaderModel < Requested)
    return unsupported("shader model");

  if (Manifest.Features & uint64_t(F::Doubles)) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS Options{};
    if (auto Err = checkQuery(
            Query(D3D12_FEATURE_D3D12_OPTIONS, &Options, sizeof(Options)),
            "CheckFeatureSupport(D3D12_OPTIONS)"))
      return Err;
    if (!Options.DoublePrecisionFloatShaderOps)
      return unsupported("Doubles");
  }
  if (Manifest.Features & (uint64_t(F::WaveOps) | uint64_t(F::Int64Ops))) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 Options{};
    if (auto Err = checkQuery(
            Query(D3D12_FEATURE_D3D12_OPTIONS1, &Options, sizeof(Options)),
            "CheckFeatureSupport(D3D12_OPTIONS1)"))
      return Err;
    if ((Manifest.Features & uint64_t(F::WaveOps)) && !Options.WaveOps)
      return unsupported("WaveOps");
    if ((Manifest.Features & uint64_t(F::Int64Ops)) && !Options.Int64ShaderOps)
      return unsupported("Int64Ops");
  }
  if (Manifest.Features & uint64_t(F::NativeLowPrecision)) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 Options{};
    if (auto Err = checkQuery(
            Query(D3D12_FEATURE_D3D12_OPTIONS4, &Options, sizeof(Options)),
            "CheckFeatureSupport(D3D12_OPTIONS4)"))
      return Err;
    if (!Options.Native16BitShaderOpsSupported)
      return unsupported("NativeLowPrecision");
  }
  if (Manifest.Features & uint64_t(F::AtomicInt64OnGroupShared)) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 Options{};
    if (auto Err = checkQuery(
            Query(D3D12_FEATURE_D3D12_OPTIONS9, &Options, sizeof(Options)),
            "CheckFeatureSupport(D3D12_OPTIONS9)"))
      return Err;
    if (!Options.AtomicInt64OnGroupSharedSupported)
      return unsupported("AtomicInt64OnGroupShared");
  }
  return Error::success();
}

} // namespace llvm::omp::target::plugin::directx
