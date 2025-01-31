//===- DirectXInstructionSelector.cpp ------------------------------*- C++
//-*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the targeting of the InstructionSelector class for
// DirectX.
//
//===----------------------------------------------------------------------===//

#include "DirectXTargetMachine.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h" // need for DirectXGenGlobalISel.inc
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "DXILConstants.h"
#include "MCTargetDesc/DirectXMCTargetDesc.h"

#define DEBUG_TYPE "directx-isel"

using namespace llvm;

#define GET_GLOBALISEL_PREDICATE_BITSET
#include "DirectXGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATE_BITSET

class DirectXInstructionSelector : public InstructionSelector {
  const DirectXSubtarget &STI;
  const DirectXInstrInfo &TII;
  const DirectXRegisterInfo &TRI;
  const RegisterBankInfo &RBI;

public:
  DirectXInstructionSelector(const DirectXTargetMachine &TM,
                             const DirectXSubtarget &ST,
                             const RegisterBankInfo &RBI);
  // Common selection code. Instruction-specific selection occurs in spvSelect.
  bool select(MachineInstr &I) override;
  // required for GIMatchTableExecutorImpl.h
  static const char *getName() { return DEBUG_TYPE; }

#define GET_GLOBALISEL_PREDICATES_DECL
#include "DirectXGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_DECL

#define GET_GLOBALISEL_TEMPORARIES_DECL
#include "DirectXGenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_DECL

private:
  // tblgen-erated 'select' implementation, used as the initial selector for
  // the patterns that don't require complex C++.
  bool selectImpl(MachineInstr &I, CodeGenCoverage &CoverageInfo) const;

  bool dxilSelect(Register ResVReg, MachineInstr &I) const;
  bool dxilSelectOp(Register ResVReg, MachineInstr &I, int MCIDOpcode, dxil::OpCode Op) const;
};

#define GET_GLOBALISEL_IMPL
#include "DirectXGenGlobalISel.inc"
#undef GET_GLOBALISEL_IMPL

DirectXInstructionSelector::DirectXInstructionSelector(
    const DirectXTargetMachine &TM, const DirectXSubtarget &ST,
    const RegisterBankInfo &RBInfo)
    : InstructionSelector(), STI(ST), TII(*ST.getInstrInfo()),
      TRI(*ST.getRegisterInfo()), RBI(RBInfo),
#define GET_GLOBALISEL_PREDICATES_INIT
#include "DirectXGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_INIT
#define GET_GLOBALISEL_TEMPORARIES_INIT
#include "DirectXGenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_INIT
{
}

bool DirectXInstructionSelector::select(MachineInstr &I) { 

  bool HasDefs = I.getNumDefs() > 0;
  Register ResVReg = HasDefs ? I.getOperand(0).getReg() : Register(0);
  assert(!HasDefs || I.getOpcode() == TargetOpcode::G_GLOBAL_VALUE);
  if (dxilSelect(ResVReg, I)) {
    I.removeFromParent();
    return true;
  }
  return false;
}

bool DirectXInstructionSelector::dxilSelectOp(Register ResVReg, MachineInstr &I, int MCIDOpcode, dxil::OpCode Op) const {
  MachineBasicBlock &BB = *I.getParent();
  auto MIB = BuildMI(BB, I, I.getDebugLoc(), /*I.getDesc()*/TII.get(MCIDOpcode))
            .addDef(ResVReg)
            .addImm(static_cast<uint64_t>(Op));
  const unsigned NumOps = I.getNumOperands();
  unsigned Index = 1;
  for (; Index < NumOps; ++Index)
    MIB.add(I.getOperand(Index));
  return MIB.constrainAllUses(TII, TRI, RBI);
  
}

bool DirectXInstructionSelector::dxilSelect(Register ResVReg,
                                         MachineInstr &I) const {

  const unsigned Opcode = I.getOpcode();
  switch (Opcode) {
    case TargetOpcode::G_FCOS:
      return dxilSelectOp(ResVReg, I, dxil::UnaryDXILInst, dxil::OpCode::Cos);
    case TargetOpcode::G_FSIN:
      return dxilSelectOp(ResVReg, I, dxil::UnaryDXILInst, dxil::OpCode::Sin);
    default:
      return false;
  }
}
namespace llvm {
InstructionSelector *
createDirectXInstructionSelector(const DirectXTargetMachine &TM,
                                 const DirectXSubtarget &Subtarget,
                                 const RegisterBankInfo &RBI) {
  return new DirectXInstructionSelector(TM, Subtarget, RBI);
}
} // namespace llvm
