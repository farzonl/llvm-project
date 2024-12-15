//===- DirectXLegalizerInfo.h --- SPIR-V Legalization Rules --------*- C++
//-*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the targeting of the MachineLegalizer class for SPIR-V.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_DIRECTX_DIRECTXMACHINELEGALIZER_H
#define LLVM_LIB_TARGET_DIRECTX_DIRECTXMACHINELEGALIZER_H

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"

namespace llvm {

class LLVMContext;
class DirectXSubtarget;

// This class provides the information for legalizing SPIR-V instructions.
class DirectXLegalizerInfo : public LegalizerInfo {
  const DirectXSubtarget *ST;

public:
  bool legalizeCustom(LegalizerHelper &Helper, MachineInstr &MI,
                      LostDebugLocObserver &LocObserver) const override;
  DirectXLegalizerInfo(const DirectXSubtarget &ST);
};
} // namespace llvm
#endif // LLVM_LIB_TARGET_DIRECTX_DIRECTXMACHINELEGALIZER_H
