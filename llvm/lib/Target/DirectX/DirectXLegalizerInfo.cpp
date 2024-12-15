//===- DirectXLegalizerInfo.cpp --- SPIR-V Legalization Rules ------*- C++
//-*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the targeting of the Machinelegalizer class for SPIR-V.
//
//===----------------------------------------------------------------------===//

#include "DirectXLegalizerInfo.h"
#include "DirectX.h"
#include "DirectXSubtarget.h"
#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"

using namespace llvm;
using namespace llvm::LegalizeActions;
using namespace llvm::LegalityPredicates;

DirectXLegalizerInfo::DirectXLegalizerInfo(const DirectXSubtarget &ST)
    : ST(&ST) {
  using namespace TargetOpcode;

  const LLT S16 = LLT::scalar(16);
  const LLT S32 = LLT::scalar(32);
  const LLT S64 = LLT::scalar(64);

  auto AllFloatScalars = {S16, S32};
  auto AllIntScalars = {S16, S32, S64};

  getActionDefinitionsBuilder(
      {G_FPOW,    G_FEXP2,   G_FLOG2,           G_FABS,
       G_FMINNUM, G_FMAXNUM, G_FCEIL,           G_FCOS,
       G_FSIN,    G_FTAN,    G_FACOS,           G_FASIN,
       G_FATAN,   G_FCOSH,   G_FSINH,           G_FTANH,
       G_FSQRT,   G_FFLOOR,  G_INTRINSIC_TRUNC, G_INTRINSIC_ROUNDEVEN,
       G_FMA})
      .legalFor(AllFloatScalars);

  getActionDefinitionsBuilder(
      {G_CTPOP, G_SMIN, G_SMAX, G_UMIN, G_UMAX, G_BITREVERSE})
      .legalFor(AllIntScalars);
}

bool DirectXLegalizerInfo::legalizeCustom(
    LegalizerHelper &Helper, MachineInstr &MI,
    LostDebugLocObserver &LocObserver) const {
  return false;
}
