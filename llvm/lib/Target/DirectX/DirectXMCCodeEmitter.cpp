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
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
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

void DirectXMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                             SmallVectorImpl<char> &CB,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  /*const uint64_t OpCode =*/getBinaryCodeForInstr(MI, Fixups, STI);
  /// TODO
}

MCCodeEmitter *llvm::createDirectXMCCodeEmitter(const MCInstrInfo &MCII,
                                                MCContext &Ctx) {
  return new DirectXMCCodeEmitter(MCII);
}

#include "DirectXGenMCCodeEmitter.inc"
