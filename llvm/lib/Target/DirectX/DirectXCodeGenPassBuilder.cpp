//===-- DirectXCodeGenPassBuilder.cpp -----------------------------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file contains DirectX CodeGen pipeline builder.
/// TODO: Port CodeGen passes to new pass manager.

#include "DirectXTargetMachine.h"

#include "llvm/MC/MCStreamer.h"
#include "llvm/Passes/CodeGenPassBuilder.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Scalar/Scalarizer.h"

using namespace llvm;

namespace {

class DirectXCodeGenPassBuilder
    : public CodeGenPassBuilder<DirectXCodeGenPassBuilder, DirectXTargetMachine> {
public:
  explicit DirectXCodeGenPassBuilder(DirectXTargetMachine &TM,
                                 const CGPassBuilderOption &Opts,
                                 PassInstrumentationCallbacks *PIC)
      : CodeGenPassBuilder(TM, Opts, PIC) {}
  void addIRPasses(AddIRPass &) const;
};

void DirectXCodeGenPassBuilder::addIRPasses(AddIRPass &addPass) const {
  addPass(ScalarizerPass());

  Base::addIRPasses(addPass);
}

} // namespace

Error DirectXTargetMachine::buildCodeGenPipeline(
    ModulePassManager &MPM, raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
    CodeGenFileType FileType, const CGPassBuilderOption &Opt,
    PassInstrumentationCallbacks *PIC) {
  auto CGPB = DirectXCodeGenPassBuilder(*this, Opt, PIC);
  return CGPB.buildPipeline(MPM, Out, DwoOut, FileType);
}