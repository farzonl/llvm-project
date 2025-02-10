//===-- DirectXAsmPrinter.cpp - DirectX assembly writer --------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains AsmPrinters for the DirectX backend.
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/DirectXTargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetLoweringObjectFile.h"

#include "DirectXTargetMachine.h" //

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

// The DXILAsmPrinter is mostly a stub because DXIL is just LLVM bitcode which
// gets embedded into a DXContainer file.
class DXILAsmPrinter : public AsmPrinter {
public:
  explicit DXILAsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)) {}

  StringRef getPassName() const override { return "DXIL Assembly Printer"; }
  void emitGlobalVariable(const GlobalVariable *GV) override;
  bool runOnMachineFunction(MachineFunction &MF) override { return false; }
  void printOperand(const MachineInstr *MI, int OpNum, raw_ostream &O);
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &O) override;
};
} // namespace

void DXILAsmPrinter::emitGlobalVariable(const GlobalVariable *GV) {
  // If there is no initializer, or no explicit section do nothing
  if (!GV->hasInitializer() || GV->hasImplicitSection() || !GV->hasSection())
    return;
  // Skip the LLVM metadata
  if (GV->getSection() == "llvm.metadata")
    return;
  SectionKind GVKind = TargetLoweringObjectFile::getKindForGlobal(GV, TM);
  MCSection *TheSection = getObjFileLowering().SectionForGlobal(GV, GVKind, TM);
  OutStreamer->switchSection(TheSection);
  emitGlobalConstant(GV->getDataLayout(), GV->getInitializer());
}

void DXILAsmPrinter::printOperand(const MachineInstr *MI, int OpNum,
                                  raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNum);

  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    O << MO.getReg();
    break;

  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    break;

  case MachineOperand::MO_FPImmediate:
    O << MO.getFPImm();
    break;

  case MachineOperand::MO_MachineBasicBlock:
    O << *MO.getMBB()->getSymbol();
    break;

  case MachineOperand::MO_GlobalAddress:
    O << *getSymbol(MO.getGlobal());
    break;

  case MachineOperand::MO_BlockAddress: {
    MCSymbol *BA = GetBlockAddressSymbol(MO.getBlockAddress());
    O << BA->getName();
    break;
  }

  case MachineOperand::MO_ExternalSymbol:
    O << *GetExternalSymbolSymbol(MO.getSymbolName());
    break;

  case MachineOperand::MO_JumpTableIndex:
  case MachineOperand::MO_ConstantPoolIndex:
  default:
    llvm_unreachable("<unknown operand type>");
  }
}

bool DXILAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                     const char *ExtraCode, raw_ostream &O) {
  if (EnableDirectXGlobalIsel)
    return true;
  if (ExtraCode && ExtraCode[0])
    return true; // Invalid instruction
  printOperand(MI, OpNo, O);
  return false;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeDirectXAsmPrinter() {
  RegisterAsmPrinter<DXILAsmPrinter> X(getTheDirectXTarget());
}
