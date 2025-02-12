//=- DirectXMCInstLower.cpp - Convert SPIR-V MachineInstr to MCInst -*- C++
//-*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower SPIR-V MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "DirectXMCInstLower.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

void DirectXMCInstLower::lower(const MachineInstr *MI, MCInst &OutMI,
                               dxil::ModuleAnalysisInfo *MAI) const {
  OutMI.setOpcode(MI->getOpcode());
  // Propagate previously set flags
  OutMI.setFlags(MI->getAsmPrinterFlags());
  // const MachineFunction *MF = MI->getMF();
  unsigned E = MI->getNumOperands();
  for (unsigned I = 0; I != E; ++I) {
    const MachineOperand &MO = MI->getOperand(I);
    MCOperand MCOp;
    switch (MO.getType()) {
    default:
      llvm_unreachable("unknown operand type");
    case MachineOperand::MO_GlobalAddress:
    case MachineOperand::MO_MachineBasicBlock:
      // TODO
      break;
    case MachineOperand::MO_Register:
      MCOp = MCOperand::createReg(MO.getReg());
      break;
    case MachineOperand::MO_Immediate:
      MCOp = MCOperand::createImm(MO.getImm());
      break;
    case MachineOperand::MO_FPImmediate:
      MCOp = MCOperand::createDFPImm(
          MO.getFPImm()->getValueAPF().convertToFloat());
      break;
    case MachineOperand::MO_Metadata:
      break;
    }

    OutMI.addOperand(MCOp);
  }
}
