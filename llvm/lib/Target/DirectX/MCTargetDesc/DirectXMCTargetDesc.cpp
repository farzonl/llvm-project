//===- DirectXMCTargetDesc.cpp - DirectX Target Implementation --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains DirectX target initializer.
///
//===----------------------------------------------------------------------===//

#include "DirectXMCTargetDesc.h"
#include "DirectXContainerObjectWriter.h"
#include "TargetInfo/DirectXTargetInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCDXContainerWriter.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>

#include "DirectXInstPrinter.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define GET_INSTRINFO_MC_HELPERS
#include "DirectXGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "DirectXGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "DirectXGenRegisterInfo.inc"

namespace {

class DXILAsmBackend : public MCAsmBackend {

public:
  DXILAsmBackend(const MCSubtargetInfo &STI)
      : MCAsmBackend(llvm::endianness::little) {}
  ~DXILAsmBackend() override = default;

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createDXContainerTargetObjectWriter();
  }

  unsigned getNumFixupKinds() const override { return 0; }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    return true;
  }
};

class DirectXMCAsmInfo : public MCAsmInfo {
public:
  explicit DirectXMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
      : MCAsmInfo() {}
};

} // namespace

static MCInstPrinter *createDirectXMCInstPrinter(const Triple &T,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new DirectXInstPrinter(MAI, MII, MRI);
  return nullptr;
}

MCAsmBackend *createDXILMCAsmBackend(const Target &T,
                                     const MCSubtargetInfo &STI,
                                     const MCRegisterInfo &MRI,
                                     const MCTargetOptions &Options) {
  return new DXILAsmBackend(STI);
}

static MCSubtargetInfo *
createDirectXMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  return createDirectXMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);
}

static MCRegisterInfo *createDirectXMCRegisterInfo(const Triple &Triple) {
  return new MCRegisterInfo();
}

static MCInstrInfo *createDirectXMCInstrInfo() { return new MCInstrInfo(); }

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeDirectXTargetMC() {
  Target &T = getTheDirectXTarget();
  RegisterMCAsmInfo<DirectXMCAsmInfo> X(T);
  TargetRegistry::RegisterMCInstrInfo(T, createDirectXMCInstrInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createDirectXMCInstPrinter);
  TargetRegistry::RegisterMCRegInfo(T, createDirectXMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createDirectXMCSubtargetInfo);
  TargetRegistry::RegisterMCCodeEmitter(T, createDirectXMCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createDXILMCAsmBackend);
}
