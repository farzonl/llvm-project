//===- DirectXWorkgroup.h - Compiler-backed workgroup variants ----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef OMPTARGET_PLUGINS_DIRECTX_WORKGROUP_H
#define OMPTARGET_PLUGINS_DIRECTX_WORKGROUP_H

#include "DirectXManifest.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm::omp::target::plugin::directx {

Expected<std::unique_ptr<MemoryBuffer>>
readWorkgroupIR(const object::OffloadBinary &Image,
                const DirectXManifestTy &Manifest);

Expected<std::unique_ptr<MemoryBuffer>>
compileWorkgroup(StringRef IR, const std::array<uint32_t, 3> &Shape);

} // namespace llvm::omp::target::plugin::directx

#endif
