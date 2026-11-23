//===- DirectXWorkgroup.cpp - Compiler-backed workgroup variants
//------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Read LLVM's COFF definitions before including Windows SDK headers.
#include "llvm/Bitcode/BitcodeReader.h"

#include "DirectXWorkgroup.h"
#include "OffloadError.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/Base64.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>

using namespace llvm;
using namespace llvm::omp::target::plugin::directx;

static std::string shapeString(const std::array<uint32_t, 3> &Shape) {
  return std::to_string(Shape[0]) + "," + std::to_string(Shape[1]) + "," +
         std::to_string(Shape[2]);
}

Expected<std::unique_ptr<MemoryBuffer>>
llvm::omp::target::plugin::directx::readWorkgroupIR(
    const object::OffloadBinary &Image, const DirectXManifestTy &Manifest) {
  auto Invalid = [](const Twine &Message) {
    return error::createOffloadError(error::ErrorCode::INVALID_BINARY,
                                     "invalid DirectX workgroup IR: %s",
                                     Message.str().c_str());
  };
  std::optional<StringRef> Encoded;
  for (const auto &[Key, Value] : Image.strings())
    if (Key == offloading::directx::WorkgroupIRKey)
      Encoded = Value;
  if (!Encoded)
    return std::unique_ptr<MemoryBuffer>();
  if (Encoded->empty())
    return Invalid("empty recipe");
  if (Image.getOffloadKind() != object::OFK_SYCL || !Manifest.Capture)
    return Invalid("a SYCL capture image is required");
  std::vector<char> Bytes;
  if (auto Err = decodeBase64(*Encoded, Bytes))
    return Invalid(toString(std::move(Err)));
  auto Buffer =
      MemoryBuffer::getMemBufferCopy(StringRef(Bytes.data(), Bytes.size()));
  LLVMContext Context;
  auto Module = parseBitcodeFile(Buffer->getMemBufferRef(), Context);
  if (!Module)
    return Invalid(toString(Module.takeError()));
  std::string Message;
  raw_string_ostream OS(Message);
  if (verifyModule(**Module, &OS))
    return Invalid(Message);
  Function *Kernel = (*Module)->getFunction(Manifest.Entry);
  if (!Kernel || Kernel->isDeclaration() ||
      !Kernel->hasFnAttribute("dx.kernel") ||
      !Kernel->hasFnAttribute(
          offloading::directx::WorkgroupSpecializableAttr) ||
      !Kernel->hasFnAttribute("hlsl.numthreads") ||
      Kernel->getFnAttribute("hlsl.numthreads").getValueAsString() !=
          shapeString(Manifest.NumThreads))
    return Invalid("entry point or default workgroup does not match the image");
  unsigned Kernels = 0;
  for (const Function &F : **Module)
    Kernels += F.hasFnAttribute("dx.kernel");
  const Triple &Target = (*Module)->getTargetTriple();
  auto Model = Target.getOSVersion();
  if (Kernels != 1 || !Target.isDXIL() ||
      Target.getOS() != Triple::ShaderModel ||
      Target.getEnvironment() != Triple::Compute ||
      Model.getMajor() != Manifest.ShaderModel[0] ||
      Model.getMinor().value_or(0) != Manifest.ShaderModel[1])
    return Invalid("target or kernel count does not match the image");
  auto *Layout = Kernel->getMetadata("dx.capture.layout");
  auto *SizeMD = Layout && Layout->getNumOperands() >= 3
                     ? dyn_cast<ConstantAsMetadata>(Layout->getOperand(0))
                     : nullptr;
  auto *Size = SizeMD ? dyn_cast<ConstantInt>(SizeMD->getValue()) : nullptr;
  if (!Size || !Size->getType()->isIntegerTy(32) ||
      Size->getZExtValue() != Manifest.Capture->Size)
    return Invalid("capture record does not match the image");
  auto *PointerMD = dyn_cast<ConstantAsMetadata>(Layout->getOperand(1));
  auto *PointerSize =
      PointerMD ? dyn_cast<ConstantInt>(PointerMD->getValue()) : nullptr;
  if (!PointerSize || !PointerSize->getType()->isIntegerTy(32) ||
      PointerSize->getZExtValue() != 8 || Kernel->arg_size() != 1 ||
      !Kernel->getArg(0)->hasByValAttr())
    return Invalid("capture ABI does not match the image");
  for (unsigned I = 0; I < Manifest.Capture->Offsets.size(); ++I) {
    uint32_t Offset = Manifest.Capture->Offsets[I];
    const auto *Scalar =
        std::get_if<DirectXScalarBindingTy>(&Manifest.Arguments[I].Binding);
    uint32_t Width = Scalar ? Scalar->Size : 8;
    bool Found = false;
    for (unsigned Field = 2; Field < Layout->getNumOperands(); ++Field) {
      auto *MD = dyn_cast<MDNode>(Layout->getOperand(Field));
      if (!MD || MD->getNumOperands() < 2)
        return Invalid("malformed capture field");
      auto *FieldOffset = mdconst::dyn_extract<ConstantInt>(MD->getOperand(0));
      auto *FieldSize = mdconst::dyn_extract<ConstantInt>(MD->getOperand(1));
      if (!FieldOffset || !FieldSize ||
          !FieldOffset->getType()->isIntegerTy(32) ||
          !FieldSize->getType()->isIntegerTy(32))
        return Invalid("malformed capture field");
      Found |= FieldOffset->getZExtValue() == Offset &&
               FieldSize->getZExtValue() == Width;
    }
    if (!Found)
      return Invalid("capture field does not match the image");
  }
  return Buffer;
}

Expected<std::unique_ptr<MemoryBuffer>>
llvm::omp::target::plugin::directx::compileWorkgroup(
    StringRef IR, const std::array<uint32_t, 3> &Shape) {
  uint64_t Product = 1;
  for (unsigned I = 0; I < 3; ++I) {
    if (!Shape[I] || Shape[I] > (I == 2 ? 64u : 1024u))
      return error::createOffloadError(error::ErrorCode::INVALID_ARGUMENT,
                                       "invalid DirectX workgroup dimensions");
    Product *= Shape[I];
  }
  if (Product > 1024)
    return error::createOffloadError(error::ErrorCode::INVALID_ARGUMENT,
                                     "DirectX workgroup exceeds 1024 threads");
  SmallVector<StringRef> Paths;
  if (const char *Path = std::getenv("OFFLOAD_DIRECTX_COMPILER_PATH"))
    Paths.push_back(Path);
  auto Find = [&](StringRef Name,
                  ArrayRef<StringRef> SearchPaths) -> Expected<std::string> {
    auto Tool = sys::findProgramByName(Name, SearchPaths);
    if (!Tool)
      return error::createOffloadError(
          error::ErrorCode::UNSUPPORTED,
          "cannot find DirectX specialization tool '%s': %s",
          Name.str().c_str(), Tool.getError().message().c_str());
    return *Tool;
  };
  auto Opt = Find("opt", Paths);
  if (!Opt)
    return Opt.takeError();
  auto LLC = Find("llc", Paths);
  if (!LLC)
    return LLC.takeError();
  auto Packager = Find("llvm-offload-binary", Paths);
  if (!Packager)
    return Packager.takeError();
  const char *ValidatorName = std::getenv("OFFLOAD_DIRECTX_DXIL_VALIDATOR");
  auto Validator = Find(ValidatorName ? ValidatorName : "dxv", {});
  if (!Validator)
    return Validator.takeError();

  SmallString<128> Model;
  sys::path::system_temp_directory(false, Model);
  sys::path::append(Model, "llvm-directx-workgroup-%%%%%%");
  SmallString<128> Directory;
  if (auto EC = sys::fs::createUniqueDirectory(Model, Directory))
    return error::createOffloadError(
        error::ErrorCode::BACKEND_FAILURE,
        "cannot create specialization directory: %s", EC.message().c_str());
  auto Path = [&](StringRef Name) {
    SmallString<128> Result(Directory);
    sys::path::append(Result, Name);
    return Result.str().str();
  };
  const std::string Input = Path("input.bc"), Kernel = Path("kernel.bc"),
                    Unsigned = Path("unsigned.dxil"),
                    Signed = Path("signed.dxil"), Output = Path("output.bin"),
                    Stdout = Path("stdout.log"), Stderr = Path("stderr.log");
  auto Cleanup = scope_exit([&] {
    for (const auto &File :
         {Input, Kernel, Unsigned, Signed, Output, Stdout, Stderr})
      if (auto EC = sys::fs::remove(File))
        errs() << "DirectX specialization cleanup: " << EC.message() << '\n';
    if (auto EC = sys::fs::remove(Directory))
      errs() << "DirectX specialization cleanup: " << EC.message() << '\n';
  });
  {
    std::error_code EC;
    raw_fd_ostream OS(Input, EC, sys::fs::OF_None);
    if (EC)
      return error::createOffloadError(error::ErrorCode::BACKEND_FAILURE,
                                       "cannot write workgroup IR: %s",
                                       EC.message().c_str());
    OS << IR;
    OS.close();
    if (OS.has_error()) {
      OS.clear_error();
      return error::createOffloadError(error::ErrorCode::BACKEND_FAILURE,
                                       "cannot write complete workgroup IR");
    }
  }
  auto Run = [&](const std::string &Tool,
                 ArrayRef<StringRef> Arguments) -> Error {
    SmallVector<StringRef> Command{Tool};
    llvm::append_range(Command, Arguments);
    std::optional<StringRef> Redirects[] = {std::nullopt, Stdout, Stderr};
    std::string Message;
    bool Failed = false;
    int Status = sys::ExecuteAndWait(Tool, Command, std::nullopt, Redirects, 0,
                                     0, &Message, &Failed);
    if (!Failed && Status == 0)
      return Error::success();
    std::string Diagnostics;
    for (const auto &File : {Stdout, Stderr}) {
      auto Log = MemoryBuffer::getFile(File);
      Diagnostics += Log ? (*Log)->getBuffer().str() : Log.getError().message();
    }
    return error::createOffloadError(
        error::ErrorCode::BACKEND_FAILURE,
        "DirectX workgroup specialization tool '%s' failed (%d): %s%s",
        Tool.c_str(), Status, Message.c_str(), Diagnostics.c_str());
  };
  std::string Option = "-dxil-kernel-numthreads=" + shapeString(Shape);
  if (auto Err = Run(*Opt, {"-dxil-kernel-abi", Option, Input, "-o", Kernel}))
    return std::move(Err);
  if (auto Err = Run(*LLC, {"-filetype=obj", Kernel, "-o", Unsigned}))
    return std::move(Err);
  std::string ValidatorOutput = "-o=" + Signed;
  if (auto Err = Run(*Validator, {ValidatorOutput, Unsigned}))
    return std::move(Err);
  LLVMContext Context;
  auto Module = parseBitcodeFile(MemoryBufferRef(IR, "workgroup"), Context);
  if (!Module)
    return Module.takeError();
  std::string Image = "--image=file=" + Signed + ",kind=sycl,triple=" +
                      (*Module)->getTargetTriple().str() +
                      ",directx-metadata=" + Kernel;
  if (auto Err = Run(*Packager, {"-o", Output, Image}))
    return std::move(Err);
  auto Result = MemoryBuffer::getFile(Output);
  if (!Result)
    return error::createOffloadError(error::ErrorCode::BACKEND_FAILURE,
                                     "cannot read specialized package: %s",
                                     Result.getError().message().c_str());
  return MemoryBuffer::getMemBufferCopy((*Result)->getBuffer());
}
