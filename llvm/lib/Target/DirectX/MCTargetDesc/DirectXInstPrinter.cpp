//===-- DirectXInstPrinter.cpp - Output DirectX MCInsts as ASM -----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class prints a DirectX MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "DirectXInstPrinter.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSymbol.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

// Include the auto-generated portion of the assembly writer.
#include "DirectXGenAsmWriter.inc"

void DirectXInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                   StringRef Annot, const MCSubtargetInfo &STI,
                                   raw_ostream &O) {

  // Without GlobalIsel DirectXInstPrinter is a null stub because DXIL
  // instructions aren't printed.
  if (!EnableDirectXGlobalIsel)
    return;

  printInstruction(MI, Address, O);
}

void DirectXInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                      raw_ostream &O, const char *Modifier) {
  assert((Modifier == 0 || Modifier[0] == 0) && "No modifiers supported");
  if (OpNo < MI->getNumOperands()) {
    const MCOperand &Op = MI->getOperand(OpNo);
    if (Op.isReg())
      O << '%' << (Op.getReg() + 1);
    else if (Op.isImm())
      O << formatImm((int64_t)Op.getImm());
    else if (Op.isDFPImm())
      O << formatImm((double)Op.getDFPImm());
    else if (Op.isExpr())
      O << Op.getExpr();
    else
      llvm_unreachable("Unexpected operand type");
  }
}