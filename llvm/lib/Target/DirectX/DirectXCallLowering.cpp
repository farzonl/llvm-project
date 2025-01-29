//===--- DirectXCallLowering.cpp - Call lowering ------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the lowering of LLVM calls to machine code calls for
// GlobalISel.
//
//===----------------------------------------------------------------------===//

#include "DirectXCallLowering.h"
#include "DirectXTargetLowering.h"
#include "DirectXTargetMachine.h"
#include "MCTargetDesc/DirectXMCTargetDesc.h"

using namespace llvm;

DirectXCallLowering::DirectXCallLowering(const DirectXTargetLowering &TLI)
    : CallLowering(&TLI) {}

bool DirectXCallLowering::lowerFormalArguments(
    MachineIRBuilder &MIRBuilder, const Function &F,
    ArrayRef<ArrayRef<Register>> VRegs, FunctionLoweringInfo &FLI) const {
  return true;
}

static std::string typeIdToName(llvm::Type *Ty) {
  switch (Ty->getTypeID()) {
  case llvm::Type::VoidTyID:
    return "void";
  case llvm::Type::HalfTyID:
    return "half";
  case llvm::Type::FloatTyID:
    return "float";
  case llvm::Type::DoubleTyID:
    return "double";
  case llvm::Type::IntegerTyID:
    return "int" + std::to_string(Ty->getIntegerBitWidth());
  default:
    llvm::report_fatal_error("unsupported type");
  }
}

bool DirectXCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                      const Value *Val,
                                      ArrayRef<Register> VRegs,
                                      FunctionLoweringInfo &FLI,
                                      Register SwiftErrorVReg) const {

  if (VRegs.size() > 1)
    return false;
  if (Val) {
    llvm::Type *RetTy = Val->getType();
    llvm::Metadata *OpType =
        llvm::MDString::get(MIRBuilder.getContext(), typeIdToName(RetTy));
    llvm::MDNode *OpTypeNode =
        llvm::MDNode::get(MIRBuilder.getContext(), OpType);
    const auto &STI = MIRBuilder.getMF().getSubtarget();
    return MIRBuilder.buildInstr(dxil::ReturnValueDXILInst)
        .addUse(VRegs[0])
        // Save type as meta data for now.
        .addMetadata(OpTypeNode)
        .constrainAllUses(MIRBuilder.getTII(), *STI.getRegisterInfo(),
                          *STI.getRegBankInfo());
  }
  MIRBuilder.buildInstr(dxil::ReturnDXILInst);
  return true;
}

bool DirectXCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                    CallLoweringInfo &Info) const {
  return true;
}