//===--- DirectXCallLowering.cpp - Call lowering -----------------*- C++-*-===//
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
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include <cstdint>

using namespace llvm;

DirectXCallLowering::DirectXCallLowering(const DirectXTargetLowering &TLI)
    : CallLowering(&TLI) {}

static std::string typeIdToName(Type *Ty) {
  switch (Ty->getTypeID()) {
  case Type::VoidTyID:
    return "void";
  case Type::HalfTyID:
    return "half";
  case Type::FloatTyID:
    return "float";
  case Type::DoubleTyID:
    return "double";
  case Type::IntegerTyID:
    return "i" + std::to_string(Ty->getIntegerBitWidth());
  case Type::FixedVectorTyID: {
    auto *VecTy = static_cast<FixedVectorType *>(Ty);
    Type *ElementType = VecTy->getElementType();
    return "<" + std::to_string(VecTy->getNumElements()) + " x " +
           typeIdToName(ElementType) + ">";
  }
  default:
    report_fatal_error("unsupported type");
  }
}

static std::string typeIdTShortName(Type *Ty) {
  switch (Ty->getTypeID()) {
  case Type::HalfTyID:
    return "f16";
  case Type::FloatTyID:
    return "f32";
  case Type::DoubleTyID:
    return "f64";
  case Type::IntegerTyID:
    return "i" + std::to_string(Ty->getIntegerBitWidth());
  case llvm::Type::FixedVectorTyID: {
    auto *VecTy = static_cast<FixedVectorType *>(Ty);
    Type *ElementType = VecTy->getElementType();
    return "v" + std::to_string(VecTy->getNumElements()) +
           typeIdTShortName(ElementType);
  }
  default:
    report_fatal_error("unsupported type");
  }
}

static uint64_t typeIdToAlignment(Type *Ty) {
  switch (Ty->getTypeID()) {
  case Type::HalfTyID:
    return 2;
  case Type::FloatTyID:
    return 4;
  case Type::DoubleTyID:
    return 8;
  case Type::IntegerTyID:
    switch (Ty->getIntegerBitWidth()) {
    case 8:
      return 1;
    case 16:
      return 2;
    case 32:
      return 4;
    case 64:
      return 8;
    }
  case Type::FixedVectorTyID: {
    auto *VecTy = static_cast<FixedVectorType *>(Ty);
    Type *ElementType = VecTy->getElementType();
    return typeIdToAlignment(ElementType);
  }
  default:
    report_fatal_error("unsupported type");
  }
}

bool DirectXCallLowering::lowerFormalArguments(
    MachineIRBuilder &MIRBuilder, const Function &F,
    ArrayRef<ArrayRef<Register>> VRegs, FunctionLoweringInfo &FLI) const {

  MachineBasicBlock &MBB = MIRBuilder.getMBB();
  if (F.isDeclaration())
    return false;

  if (!MBB.isEntryBlock())
    return false;

  auto &BB = F.getEntryBlock();
  // DenseMap<std::string, const AllocaInst*> AllocaMap;
  std::map<std::string, const AllocaInst *> AllocaMap;
  for (const Instruction &I : BB) {
    if (const AllocaInst *AI = dyn_cast<const AllocaInst>(&I))
      AllocaMap[AI->getName().str()] = AI;
  }

  const auto &STI = MIRBuilder.getMF().getSubtarget();
  unsigned VRegsIndex = 0;
  for (const Argument &Arg : F.args()) {
    if (MIRBuilder.getDataLayout().getTypeStoreSize(Arg.getType()).isZero())
      continue; // Don't handle zero sized types.

    Type *ArgTy = Arg.getType();

    auto MIB = MIRBuilder.buildInstr(dxil::AllocaDXILInst);
    MIB.addDef(VRegs[VRegsIndex][0]).addImm(ArgTy->getTypeID());
    auto It = AllocaMap.find(Arg.getName().str());
    if (It == AllocaMap.end()) {
      MIB.addImm(typeIdToAlignment(Arg.getType()));
    } else {
      MIB.addImm(It->second->getAlign().value());
    }
    Metadata *OpType =
        MDString::get(MIRBuilder.getContext(), typeIdTShortName(ArgTy));
    MDNode *OpTypeNode = MDNode::get(MIRBuilder.getContext(), OpType);
    MIB.addMetadata(OpTypeNode);
    MIB.constrainAllUses(MIRBuilder.getTII(), *STI.getRegisterInfo(),
                         *STI.getRegBankInfo());

    ++VRegsIndex;
  }
  return true;
}

bool DirectXCallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                      const Value *Val,
                                      ArrayRef<Register> VRegs,
                                      FunctionLoweringInfo &FLI,
                                      Register SwiftErrorVReg) const {

  if (VRegs.size() > 1)
    return false;
  if (Val) {
    Type *RetTy = Val->getType();
    Metadata *OpType =
        MDString::get(MIRBuilder.getContext(), typeIdToName(RetTy));
    MDNode *OpTypeNode = MDNode::get(MIRBuilder.getContext(), OpType);
    const auto &STI = MIRBuilder.getMF().getSubtarget();
    return MIRBuilder.buildInstr(dxil::ReturnValueDXILInst)
        .addImm(RetTy->getTypeID())
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
