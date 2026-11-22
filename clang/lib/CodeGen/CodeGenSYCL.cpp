//===--------- CodeGenSYCL.cpp - Code for SYCL kernel generation ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code required for generation of SYCL kernel caller offload
// entry point functions.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "TargetInfo.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/Frontend/Offloading/OffloadWrapper.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <cassert>

using namespace clang;
using namespace CodeGen;

void CodeGenFunction::EmitSYCLKernelCallStmt(const SYCLKernelCallStmt &S) {
  // SYCLKernelCallStmt instances are only injected in the definitions of
  // functions declared with the sycl_kernel_entry_point attribute. ODR-use of
  // such a function in code emitted during device compilation should be
  // diagnosed. Thus, any attempt to emit a SYCLKernelCallStmt during device
  // compilation indicates a missing diagnostic.
  assert(!getLangOpts().SYCLIsDevice &&
         "Attempt to emit a SYCL kernel call statement during device"
         " compilation");
  EmitStmt(S.getKernelLaunchStmt());
}

static void SetSYCLKernelAttributes(llvm::Function *Fn, CodeGenFunction &CGF) {
  // SYCL 2020 device language restrictions require forward progress and
  // disallow recursion.
  Fn->setDoesNotRecurse();
  if (CGF.checkIfFunctionMustProgress())
    Fn->addFnAttr(llvm::Attribute::MustProgress);
}

void CodeGenModule::EmitSYCLKernelCaller(const FunctionDecl *KernelEntryPointFn,
                                         ASTContext &Ctx) {
  assert(Ctx.getLangOpts().SYCLIsDevice &&
         "SYCL kernel caller offload entry point functions can only be emitted"
         " during device compilation");

  const auto *KernelEntryPointAttr =
      KernelEntryPointFn->getAttr<SYCLKernelEntryPointAttr>();
  assert(KernelEntryPointAttr && "Missing sycl_kernel_entry_point attribute");
  assert(!KernelEntryPointAttr->isInvalidAttr() &&
         "sycl_kernel_entry_point attribute is invalid");

  // Find the SYCLKernelCallStmt.
  SYCLKernelCallStmt *KernelCallStmt =
      cast<SYCLKernelCallStmt>(KernelEntryPointFn->getBody());

  // Retrieve the SYCL kernel caller parameters from the OutlinedFunctionDecl.
  FunctionArgList Args;
  const OutlinedFunctionDecl *OutlinedFnDecl =
      KernelCallStmt->getOutlinedFunctionDecl();
  Args.append(OutlinedFnDecl->param_begin(), OutlinedFnDecl->param_end());

  // Compute the function info and LLVM function type.
  const CGFunctionInfo &FnInfo =
      getTypes().arrangeDeviceKernelCallerDeclaration(Ctx.VoidTy, Args);
  llvm::FunctionType *FnTy = getTypes().GetFunctionType(FnInfo);

  // Retrieve the generated name for the SYCL kernel caller function.
  CanQualType KernelNameType =
      Ctx.getCanonicalType(KernelEntryPointAttr->getKernelName());
  const SYCLKernelInfo &KernelInfo = Ctx.getSYCLKernelInfo(KernelNameType);
  auto *Fn = llvm::Function::Create(FnTy, llvm::Function::ExternalLinkage,
                                    KernelInfo.GetKernelName(), &getModule());

  // Emit the SYCL kernel caller function.
  CodeGenFunction CGF(*this);
  SetLLVMFunctionAttributes(GlobalDecl(), FnInfo, Fn, false);
  SetSYCLKernelAttributes(Fn, CGF);
  addSYCLModuleIdAttr(Fn);
  CGF.StartFunction(GlobalDecl(), Ctx.VoidTy, Fn, FnInfo, Args,
                    SourceLocation(), SourceLocation());
  CGF.EmitFunctionBody(OutlinedFnDecl->getBody());
  setDSOLocal(Fn);
  SetLLVMFunctionAttributesForDefinition(cast<Decl>(OutlinedFnDecl), Fn);
  CGF.FinishFunction();
  if (getTriple().isDXIL()) {
    getTargetCodeGenInfo().setTargetAttributes(KernelEntryPointFn, Fn, *this);
    if (!Fn->hasFnAttribute("hlsl.numthreads"))
      Fn->addFnAttr("hlsl.numthreads", "1,1,1");
    llvm::SmallVector<llvm::Metadata *> Layout;
    if (Args.size() == 1) {
      QualType CaptureType = Args[0]->getType().getNonReferenceType();
      if (CaptureType->isPointerType())
        CaptureType = CaptureType->getPointeeType();
      if (const auto *Record = CaptureType->getAs<RecordType>()) {
        const RecordDecl *RD = Record->getDecl();
        const ASTRecordLayout &RL = Ctx.getASTRecordLayout(RD);
        auto Integer = [&](uint64_t Value) {
          return llvm::ConstantAsMetadata::get(
              llvm::ConstantInt::get(Int32Ty, Value));
        };
        Layout.push_back(Integer(RL.getSize().getQuantity()));
        Layout.push_back(
            Integer(Ctx.getTargetInfo().getPointerWidth(LangAS::Default) / 8));
        for (const FieldDecl *Field : RD->fields())
          Layout.push_back(llvm::MDNode::get(
              getLLVMContext(),
              {Integer(RL.getFieldOffset(Field->getFieldIndex()) / 8),
               Integer(
                   Ctx.getTypeSizeInChars(Field->getType()).getQuantity())}));
      }
    }
    Fn->setMetadata("dx.capture.layout",
                    llvm::MDNode::get(getLLVMContext(), Layout));
  }
}

llvm::Function *CodeGenModule::embedSYCLDeviceBinary() {
  StringRef FileName = getCodeGenOpts().OffloadBinaryToEmbedFile;
  auto BufferOrErr = getFileSystem()->getBufferForFile(FileName);
  if (std::error_code EC = BufferOrErr.getError()) {
    getDiags().Report(diag::err_cannot_open_file) << FileName << EC.message();
    return nullptr;
  }
  std::unique_ptr<llvm::MemoryBuffer> Buffer = std::move(BufferOrErr.get());
  llvm::Function *RegistrationFunc = nullptr;
  if (llvm::Error Err = llvm::offloading::wrapSYCLBinaries(
          getModule(),
          ArrayRef<char>(Buffer->getBufferStart(), Buffer->getBufferSize()),
          llvm::offloading::SYCLJITOptions(), /*IsFinalizedImage=*/true,
          &RegistrationFunc)) {
    getDiags().Report(diag::err_fe_error_backend)
        << llvm::toString(std::move(Err));
    return nullptr;
  }
  return RegistrationFunc;
}
