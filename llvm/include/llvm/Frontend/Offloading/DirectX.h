//===- DirectX.h - DirectX offloading launch ABI ------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FRONTEND_OFFLOADING_DIRECTX_H
#define LLVM_FRONTEND_OFFLOADING_DIRECTX_H

#include <cstdint>

namespace llvm::offloading::directx {

// The version-1 dispatch block is nine little-endian 32-bit root constants:
// group counts (x,y,z), followed by three 64-bit global offsets (low word
// first).
inline constexpr uint32_t DispatchVersion = 1;
inline constexpr uint32_t DispatchWords = 9;
inline constexpr uint32_t DispatchBytes = DispatchWords * 4;
inline constexpr uint32_t GlobalOffsetWord = 3;

} // namespace llvm::offloading::directx

#endif
