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

#include "DirectXInstrInfo.h"
#include "DirectXMCInstLower.h"
#include "DirectXSubtarget.h"
#include "TargetInfo/DirectXFlags.h"

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
  bool runOnMachineFunction(MachineFunction &MF) override;
  // helpers
  void outputMCInst(MCInst &Inst);
  void outputInstruction(const MachineInstr *MI);

  // overides
  void emitInstruction(const MachineInstr *MI) override;
  void emitFunctionHeader() override;
  void emitFunctionBodyEnd() override;
  void emitBasicBlockStart(const MachineBasicBlock &MBB) override;

private:
  dxil::ModuleAnalysisInfo *MAI;
};
} // namespace

bool DXILAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableDirectXGlobalIsel)
    return false;
  return AsmPrinter::runOnMachineFunction(MF);
}

void DXILAsmPrinter::outputMCInst(MCInst &Inst) {
  const DirectXSubtarget *ST = &MF->getSubtarget<DirectXSubtarget>();
  OutStreamer->emitInstruction(Inst, *ST);
}

void DXILAsmPrinter::outputInstruction(const MachineInstr *MI) {
  DirectXMCInstLower MCInstLowering;
  MCInst TmpInst;
  MCInstLowering.lower(MI, TmpInst, MAI);
  outputMCInst(TmpInst);
}

void DXILAsmPrinter::emitInstruction(const MachineInstr *MI) {

  if (!EnableDirectXGlobalIsel)
    return;

  outputInstruction(MI);
}

void DXILAsmPrinter::emitBasicBlockStart(const MachineBasicBlock &MBB) {

  if (!EnableDirectXGlobalIsel)
    return;

  // Do not emit anything if it's an internal service function.
  if (MBB.empty())
    return;

  // If it's the first MBB in MF, it has OpFunction and OpFunctionParameter, so
  // OpLabel should be output after them.
  if (MBB.getNumber() == MF->front().getNumber()) {
    OutStreamer->getCommentOS()
        << "-- Begin Basic Block "
        << GlobalValue::dropLLVMManglingEscape(MBB.getName()) << '\n';
    for (const MachineInstr &MI : MBB)
      OutStreamer->getCommentOS()
          << "-- Instruction " << MI.getOpcode() << '\n';
  }
}

void DXILAsmPrinter::emitFunctionHeader() {

  if (!EnableDirectXGlobalIsel)
    return;

  // Get the subtarget from the current MachineFunction.
  // const DirectXSubtarget* ST = &MF->getSubtarget<DirectXSubtarget>();
  // const DirectXInstrInfo* TII = ST->getInstrInfo();
  const Function &F = MF->getFunction();

  if (isVerbose()) {
    OutStreamer->getCommentOS()
        << "-- Begin function "
        << GlobalValue::dropLLVMManglingEscape(F.getName()) << '\n';
  }

  // auto *Section = getObjFileLowering().SectionForGlobal(&F, TM);
  // MF->setSection(Section);
}

void DXILAsmPrinter::emitFunctionBodyEnd() {

  if (!EnableDirectXGlobalIsel)
    return;

  // Get the subtarget from the current MachineFunction.
  // const DirectXSubtarget* ST = &MF->getSubtarget<DirectXSubtarget>();
  // const DirectXInstrInfo* TII = ST->getInstrInfo();
  const Function &F = MF->getFunction();

  if (isVerbose()) {
    OutStreamer->getCommentOS()
        << "-- End function "
        << GlobalValue::dropLLVMManglingEscape(F.getName()) << '\n';
  }
}

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

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeDirectXAsmPrinter() {
  RegisterAsmPrinter<DXILAsmPrinter> X(getTheDirectXTarget());
}
