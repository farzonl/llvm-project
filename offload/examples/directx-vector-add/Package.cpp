//===- Package.cpp - Package a DirectX demo shader
//--------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

int main(int Argc, char **Argv) {
  if (Argc != 4) {
    errs() << "Usage: directx-package <shader.dxil> <kernel.bc> <output.bin>\n";
    return 1;
  }
  auto Shader = MemoryBuffer::getFile(Argv[1], false);
  if (!Shader) {
    errs() << Argv[1] << ": " << Shader.getError().message() << '\n';
    return 1;
  }
  auto Kernel = MemoryBuffer::getFile(Argv[2], false);
  if (!Kernel) {
    errs() << Argv[2] << ": " << Kernel.getError().message() << '\n';
    return 1;
  }
  LLVMContext Context;
  auto Module = parseBitcodeFile((*Kernel)->getMemBufferRef(), Context);
  if (!Module) {
    errs() << Argv[2] << ": " << toString(Module.takeError()) << '\n';
    return 1;
  }
  auto *Named = (*Module)->getNamedMetadata("llvm.offload.manifest");
  auto *MD = Named && Named->getNumOperands() == 1 &&
                     Named->getOperand(0)->getNumOperands() == 1
                 ? dyn_cast<MDString>(Named->getOperand(0)->getOperand(0))
                 : nullptr;
  if (!MD) {
    errs() << Argv[2] << ": missing compiler-generated launch manifest\n";
    return 1;
  }
  StringRef Manifest = MD->getString();
  auto JSON = json::parse(Manifest);
  if (!JSON) {
    errs() << Argv[2] << ": " << toString(JSON.takeError()) << '\n';
    return 1;
  }
  auto *ManifestObject = JSON->getAsObject();
  if (!ManifestObject) {
    errs() << Argv[2] << ": launch manifest must be an object\n";
    return 1;
  }
  auto Container = object::DXContainer::create((*Shader)->getMemBufferRef());
  if (!Container) {
    errs() << Argv[1] << ": " << toString(Container.takeError()) << '\n';
    return 1;
  }
  auto Flags = Container->getShaderFeatureFlags();
  if (!Flags) {
    errs() << Argv[1] << ": missing shader feature flags\n";
    return 1;
  }
  json::Array Features;
  uint64_t Remaining = *Flags;
#define SHADER_FEATURE_FLAG(Num, ModuleNum, Name, Description)                 \
  if (StringRef(#Name) != "RESERVED" && StringRef(#Name) != "NextUnusedBit") { \
    uint64_t Flag = static_cast<uint64_t>(dxbc::FeatureFlags::Name);           \
    if (Remaining & Flag) {                                                    \
      Features.push_back(#Name);                                               \
      Remaining &= ~Flag;                                                      \
    }                                                                          \
  }
#include "llvm/BinaryFormat/DXContainerConstants.def"
  if (Remaining) {
    errs() << Argv[1] << ": unsupported shader feature flags\n";
    return 1;
  }
  (*ManifestObject)["features"] = std::move(Features);
  std::string ManifestJSON;
  raw_string_ostream ManifestStream(ManifestJSON);
  ManifestStream << *JSON;
  object::OffloadBinary::OffloadingImage Image;
  Image.TheImageKind = object::IMG_DXIL;
  Image.StringData["directx.manifest"] = ManifestJSON;
  Image.Image = std::move(*Shader);
  auto Bytes = object::OffloadBinary::write(ArrayRef(Image));
  std::error_code EC;
  raw_fd_ostream Output(Argv[3], EC, sys::fs::OF_None);
  if (EC) {
    errs() << Argv[3] << ": " << EC.message() << '\n';
    return 1;
  }
  Output.write(Bytes.data(), Bytes.size());
  Output.close();
  if (Output.has_error()) {
    errs() << Argv[3] << ": " << Output.error().message() << '\n';
    Output.clear_error();
    return 1;
  }
  return 0;
}
