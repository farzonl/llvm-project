//===- DXILElementwiseExpansion.h - Prepare LLVM Module for DXIL
//encoding------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TARGET_DIRECTX_DXILELEMENTWISEEXPANSION_H
#define LLVM_TARGET_DIRECTX_DXILELEMENTWISEEXPANSION_H

#include "DXILResource.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {
class DXILElementwiseExpansionLegacy : public ModulePass {

public:
  bool runOnModule(Module &M) override;
  DXILElementwiseExpansionLegacy() : ModulePass(ID) {}

  static char ID; // Pass identification.
};
} // namespace llvm

#endif // LLVM_TARGET_DIRECTX_DXILELEMENTWISEEXPANSION_H
