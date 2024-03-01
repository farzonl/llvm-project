//===- DXILElementwiseExpansion.cpp - Prepare LLVM Module for DXIL
//encoding------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains llvm elementwise vector intrinsic expansions to
//  scalar calls amenable for lowering to LLVM 3.7-based DirectX Intermediate
//  Language (DXIL).
//===----------------------------------------------------------------------===//

#include "DXILElementwiseExpansion.h"
#include "DirectX.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"

#define DEBUG_TYPE "dxil-elementwise-expansion"

using namespace llvm;

static bool isElementWiseIntrinsic(Function &F) {
  switch (F.getIntrinsicID()) {
  case Intrinsic::sin:
    return true;
  }
  return false;
}

static bool allArgsVectors(CallInst *IntrinsicCall) {
  size_t NumOperands = IntrinsicCall->getNumOperands();
  for (size_t i = 0; i < NumOperands; i++) {
    Value *Arg = IntrinsicCall->getOperand(i);
    if (Arg->getType()->isPointerTy())
      continue;
    if (!Arg->getType()->isVectorTy())
      return false;
  }
  return true;
}

static bool expandElementwiseIntrinsic(Function &F, CallInst *OrigIntrinsic) {
  Instruction *CurrInstruction = OrigIntrinsic;
  IRBuilder<> Builder(CurrInstruction->getParent());
  Builder.SetInsertPoint(CurrInstruction);

  size_t NumOperands = OrigIntrinsic->getNumOperands();
  Value *Arg0 = OrigIntrinsic->getOperand(0);
  auto *VecArg = dyn_cast<FixedVectorType>(Arg0->getType());
  Value *Result = llvm::PoisonValue::get(llvm::FixedVectorType::get(
      VecArg->getElementType(), VecArg->getNumElements()));
  for (size_t i = 0; i < VecArg->getNumElements(); i++) {
    SmallVector<Value *, 4> ExtractedElements;
    for (size_t j = 0; j < NumOperands; j++) {
      Value *CurrArg = OrigIntrinsic->getOperand(j);
      if (CurrArg->getType()->isPointerTy())
        continue;
      Value *ArgIndex =
          ConstantInt::get(Type::getInt32Ty(CurrArg->getContext()), i);
      Value *ExtractedElement = Builder.CreateExtractElement(CurrArg, ArgIndex);
      ExtractedElements.push_back(ExtractedElement);
    }
    ArrayRef<Value *> ArgsAsArrayRef(ExtractedElements.data(),
                                     ExtractedElements.size());
    CallInst *CurrCall = Builder.CreateIntrinsic(
        /*ReturnType*/ VecArg->getElementType(), F.getIntrinsicID(),
        ArgsAsArrayRef);
    Result = Builder.CreateInsertElement(Result, CurrCall, i);
  }

  OrigIntrinsic->replaceAllUsesWith(Result);
  OrigIntrinsic->eraseFromParent();
  return true;
}

static bool elementwiseExpansion(Module &M) {
  for (auto &F : make_early_inc_range(M.functions())) {
    if (!isElementWiseIntrinsic(F))
      continue;
    bool ElementwiseExpanded = false;
    for (User *U : make_early_inc_range(F.users())) {
      auto *IntrinsicCall = dyn_cast<CallInst>(U);
      if (!IntrinsicCall || !allArgsVectors(IntrinsicCall))
        continue;
      ElementwiseExpanded = expandElementwiseIntrinsic(F, IntrinsicCall);
    }
    if (F.user_empty() && ElementwiseExpanded)
      F.eraseFromParent();
  }
  return true;
}

namespace {
/// A pass that transforms external global definitions into declarations.
class DXILElementwiseExpansion
    : public PassInfoMixin<DXILElementwiseExpansion> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    if (elementwiseExpansion(M))
      return PreservedAnalyses::none();
    return PreservedAnalyses::all();
  }
};
} // namespace

bool DXILElementwiseExpansionLegacy::runOnModule(Module &M) {
  return elementwiseExpansion(M);
}

char DXILElementwiseExpansionLegacy::ID = 0;

INITIALIZE_PASS_BEGIN(DXILElementwiseExpansionLegacy, DEBUG_TYPE,
                      "DXIL Elementwise Expansion", false, false)
INITIALIZE_PASS_END(DXILElementwiseExpansionLegacy, DEBUG_TYPE,
                    "DXIL Elementwise Expansion", false, false)

ModulePass *llvm::createDXILElementwiseExpansionLegacyPass() {
  return new DXILElementwiseExpansionLegacy();
}
