//===- DirectXTargetMachine.cpp - DirectX Target Implementation -*- C++ -*-===//
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

#include "DirectXTargetMachine.h"
#include "DXILDataScalarization.h"
#include "DXILFlattenArrays.h"
#include "DXILIntrinsicExpansion.h"
#include "DXILOpLowering.h"
#include "DXILPrettyPrinter.h"
#include "DXILResourceAccess.h"
#include "DXILResourceAnalysis.h"
#include "DXILShaderFlags.h"
#include "DXILTranslateMetadata.h"
#include "TargetInfo/DirectXFlags.h"
#include "DXILWriter/DXILWriterPass.h"
#include "DirectX.h"
#include "DirectXSubtarget.h"
#include "DirectXTargetTransformInfo.h"
#include "TargetInfo/DirectXTargetInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"

// GlobalISel Includes Start
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
// GlobalISel Includes End

#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCSectionDXContainer.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Transforms/Scalar/Scalarizer.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeDirectXTarget() {
  RegisterTargetMachine<DirectXTargetMachine> X(getTheDirectXTarget());
  auto *PR = PassRegistry::getPassRegistry();
  initializeDXILIntrinsicExpansionLegacyPass(*PR);
  initializeDXILDataScalarizationLegacyPass(*PR);
  initializeDXILFlattenArraysLegacyPass(*PR);
  initializeScalarizerLegacyPassPass(*PR);
  initializeDXILPrepareModulePass(*PR);
  initializeEmbedDXILPassPass(*PR);
  initializeWriteDXILPassPass(*PR);
  initializeDXContainerGlobalsPass(*PR);
  initializeGlobalISel(*PR);
  initializeDXILOpLoweringLegacyPass(*PR);
  initializeDXILResourceAccessLegacyPass(*PR);
  initializeDXILTranslateMetadataLegacyPass(*PR);
  initializeDXILResourceMDWrapperPass(*PR);
  initializeShaderFlagsAnalysisWrapperPass(*PR);
  initializeDXILFinalizeLinkageLegacyPass(*PR);
}

class DXILTargetObjectFile : public TargetLoweringObjectFile {
public:
  DXILTargetObjectFile() = default;

  MCSection *getExplicitSectionGlobal(const GlobalObject *GO, SectionKind Kind,
                                      const TargetMachine &TM) const override {
    return getContext().getDXContainerSection(GO->getSection(), Kind);
  }

protected:
  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override {
    llvm_unreachable("Not supported!");
  }
};

class DirectXPassConfig : public TargetPassConfig {
public:
  DirectXPassConfig(DirectXTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  DirectXTargetMachine &getDirectXTargetMachine() const {
    return getTM<DirectXTargetMachine>();
  }

  // GlobalISel Specific Passes Start
  bool addLegalizeMachineIR() override;
  bool addGlobalInstructionSelect() override;
  bool addIRTranslator() override;
  bool addRegBankSelect() override;
  void addPostRegAlloc() override;
  void addFastRegAlloc() override {}
  void addOptimizedRegAlloc() override {}
  // GlobalISel Specific Passes End

  FunctionPass *createTargetRegisterAllocator(bool) override { return nullptr; }
  void addCodeGenPrepare() override {
    addPass(createDXILFinalizeLinkageLegacyPass());
    addPass(createDXILIntrinsicExpansionLegacyPass());
    addPass(createDXILDataScalarizationLegacyPass());
    addPass(createDXILFlattenArraysLegacyPass());
    addPass(createDXILResourceAccessLegacyPass());
    ScalarizerPassOptions DxilScalarOptions;
    DxilScalarOptions.ScalarizeLoadStore = true;
    addPass(createScalarizerPass(DxilScalarOptions));
    addPass(createDXILTranslateMetadataLegacyPass());
    if (EnableDirectXGlobalIsel) {
      addCoreISelPasses();
      addMachinePasses();
    } else
      addPass(createDXILOpLoweringLegacyPass());

    addPass(createDXILPrepareModulePass());
  }
};

DirectXTargetMachine::DirectXTargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           std::optional<Reloc::Model> RM,
                                           std::optional<CodeModel::Model> CM,
                                           CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(
          T,
          "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-"
          "f32:32-f64:64-n8:16:32:64",
          TT, CPU, FS, Options, Reloc::Static, CodeModel::Small, OL),
      TLOF(std::make_unique<DXILTargetObjectFile>()),
      Subtarget(std::make_unique<DirectXSubtarget>(TT, CPU, FS, *this)) {
  initAsmInfo();
  setGlobalISel(EnableDirectXGlobalIsel == cl::BOU_TRUE);
}

DirectXTargetMachine::~DirectXTargetMachine() {}

void DirectXTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
#define GET_PASS_REGISTRY "DirectXPassRegistry.def"
#include "llvm/Passes/TargetPassRegistry.inc"
}

bool DirectXTargetMachine::addPassesToEmitFile(
    PassManagerBase &PM, raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
    CodeGenFileType FileType, bool DisableVerify,
    MachineModuleInfoWrapperPass *MMIWP) {
  TargetPassConfig *PassConfig = createPassConfig(PM);
  // Set PassConfig options provided by TargetMachine.
  PassConfig->setDisableVerify(DisableVerify);
  PM.add(PassConfig);
  // Need to sooner for Analysis Passes
  if (!MMIWP)
    MMIWP = new MachineModuleInfoWrapperPass(this);
  PM.add(MMIWP);
  PassConfig->addCodeGenPrepare();
  PassConfig->setInitialized();

  if (EnableDirectXGlobalIsel && FileType != CodeGenFileType::Null) {
    PM.add(createPrintMIRPass(Out));
    // return false;
  }
  switch (FileType) {
  case CodeGenFileType::AssemblyFile:
    PM.add(createDXILPrettyPrinterLegacyPass(Out));
    PM.add(createPrintModulePass(Out, "", true));
    break;
  case CodeGenFileType::ObjectFile:
    if (TargetPassConfig::willCompleteCodeGenPipeline()) {
      PM.add(createDXILEmbedderPass());
      // We embed the other DXContainer globals after embedding DXIL so that the
      // globals don't pollute the DXIL.
      PM.add(createDXContainerGlobalsPass());

      if (addAsmPrinter(PM, Out, DwoOut, FileType,
                        MMIWP->getMMI().getContext()))
        return true;
    } else
      PM.add(createDXILWriterPass(Out));
    break;
  case CodeGenFileType::Null:
    break;
  }

  return false;
}

bool DirectXTargetMachine::addPassesToEmitMC(PassManagerBase &PM,
                                             MCContext *&Ctx,
                                             raw_pwrite_stream &Out,
                                             bool DisableVerify) {
  return true;
}

TargetPassConfig *DirectXTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new DirectXPassConfig(*this, PM);
}

const DirectXSubtarget *
DirectXTargetMachine::getSubtargetImpl(const Function &) const {
  return Subtarget.get();
}

TargetTransformInfo
DirectXTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(DirectXTTIImpl(this, F));
}

DirectXTargetLowering::DirectXTargetLowering(const DirectXTargetMachine &TM,
                                             const DirectXSubtarget &STI)
    : TargetLowering(TM) {}

namespace {
// A custom subclass of InstructionSelect, which is mostly the same except from
// not requiring RegBankSelect to occur previously.
class DirectXInstructionSelect : public InstructionSelect {
  // We don't use register banks, so unset the requirement for them
  MachineFunctionProperties getRequiredProperties() const override {
    return InstructionSelect::getRequiredProperties().reset(
        MachineFunctionProperties::Property::RegBankSelected);
  }
};
} // namespace

bool DirectXPassConfig::addGlobalInstructionSelect() {
  if (EnableDirectXGlobalIsel) {
    addPass(new DirectXInstructionSelect());
    return false;
  }
  return true;
}

// Do not add the RegBankSelect pass, as we only ever need virtual registers.
bool DirectXPassConfig::addRegBankSelect() {
  if (EnableDirectXGlobalIsel) {
    disablePass(&RegBankSelect::ID);
    return false;
  }
  return true;
}

// Disable passes that break from assuming no virtual registers exist.
void DirectXPassConfig::addPostRegAlloc() {
  // Do not work with vregs instead of physical regs.
  disablePass(&MachineCopyPropagationID);
  disablePass(&PostRAMachineSinkingID);
  disablePass(&PostRASchedulerID);
  disablePass(&FuncletLayoutID);
  disablePass(&StackMapLivenessID);
  disablePass(&PatchableFunctionID);
  disablePass(&ShrinkWrapID);
  disablePass(&LiveDebugValuesID);
  disablePass(&MachineLateInstrsCleanupID);
  disablePass(&RemoveLoadsIntoFakeUsesID);

  // Do not work with OpPhi.
  disablePass(&BranchFolderPassID);
  disablePass(&MachineBlockPlacementID);

  TargetPassConfig::addPostRegAlloc();
}

bool DirectXPassConfig::addIRTranslator() {
  if (EnableDirectXGlobalIsel) {
    addPass(new IRTranslator(getOptLevel()));
    return false;
  }
  return true;
}

// Use the default legalizer.
bool DirectXPassConfig::addLegalizeMachineIR() {
  if (EnableDirectXGlobalIsel) {
    addPass(new Legalizer());
    return false;
  }
  return true;
}
