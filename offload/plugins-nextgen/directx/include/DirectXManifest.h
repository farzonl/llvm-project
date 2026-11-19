//===- DirectXManifest.h - DirectX launch ABI ---------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef OMPTARGET_PLUGINS_DIRECTX_MANIFEST_H
#define OMPTARGET_PLUGINS_DIRECTX_MANIFEST_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace llvm::object {
class OffloadBinary;
}

namespace llvm::omp::target::plugin::directx {

struct DirectXBufferBindingTy {
  enum class Kind { SRV, UAV };
  Kind Access = Kind::SRV;
  uint32_t Register = 0;
  uint32_t Space = 0;
  uint32_t MinSize = 0;
};

struct DirectXScalarBindingTy {
  uint32_t Size = 0;
  uint32_t Offset = 0;
};

struct DirectXArgumentTy {
  uint32_t Index = 0;
  std::variant<DirectXBufferBindingTy, DirectXScalarBindingTy> Binding;
};

struct DirectXConstantBindingTy {
  uint32_t Register = 0;
  uint32_t Space = 0;
  uint32_t Size = 0;
};

struct DirectXManifestTy {
  static constexpr StringLiteral MetadataKey = "directx.manifest";
  static constexpr uint32_t Version = 1;
  static constexpr uint32_t MaxConstantBytes = 252;

  std::string Entry;
  std::array<uint32_t, 2> ShaderModel{};
  std::array<uint32_t, 3> NumThreads{};
  uint64_t Features = 0;
  std::optional<DirectXConstantBindingTy> Constants;
  // Canonicalized into argument-index order, independent of JSON array order.
  SmallVector<DirectXArgumentTy> Arguments;

  static Expected<DirectXManifestTy> parse(StringRef JSON);
  static Expected<DirectXManifestTy> parse(const object::OffloadBinary &Image);
};

} // namespace llvm::omp::target::plugin::directx

#endif
