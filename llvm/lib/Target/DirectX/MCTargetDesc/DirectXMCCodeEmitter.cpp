//===-- DirectXMCCodeEmitter.cpp - Emit DXIL machine code -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the DirectXMCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/DirectXMCTargetDesc.h"
#include "TargetInfo/DirectXFlags.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"

using namespace llvm;

#define DEBUG_TYPE "directx-mccodeemitter"

namespace {

class DirectXMCCodeEmitter : public MCCodeEmitter {
  const MCInstrInfo &MCII;

public:
  DirectXMCCodeEmitter(const MCInstrInfo &MCIInfo) : MCII(MCIInfo) {}
  DirectXMCCodeEmitter(const DirectXMCCodeEmitter &) = delete;
  void operator=(const DirectXMCCodeEmitter &) = delete;
  ~DirectXMCCodeEmitter() override = default;

  // getBinaryCodeForInstr - TableGen'erated function for getting the
  // binary encoding for an instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};
} // end anonymous namespace

static void emitOperand(const MCOperand &Op, SmallVectorImpl<char> &CB) {
  if (Op.isReg()) {
    // Emit the id index starting at 1 (0 is an invalid index).
    support::endian::write<uint32_t>(CB, Op.getReg() + 1,
                                     llvm::endianness::little);
  } else if (Op.isImm()) {
    support::endian::write(CB, static_cast<uint32_t>(Op.getImm()),
                           llvm::endianness::little);
  } else {
    llvm_unreachable("Unexpected operand type in VReg");
  }
}

void DirectXMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                             SmallVectorImpl<char> &CB,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  if (!EnableDirectXGlobalIsel)
    return;

  const uint64_t OpCode = getBinaryCodeForInstr(MI, Fixups, STI);
  const uint32_t NumWords = MI.getNumOperands() + 1;
  const uint32_t FirstWord = (NumWords << 16) | OpCode;
  support::endian::write(CB, FirstWord, llvm::endianness::little);
  for (const auto &Op : MI)
    emitOperand(Op, CB);
}

MCCodeEmitter *llvm::createDirectXMCCodeEmitter(const MCInstrInfo &MCII,
                                                MCContext &Ctx) {
  return new DirectXMCCodeEmitter(MCII);
}

#include "DirectXGenMCCodeEmitter.inc"
