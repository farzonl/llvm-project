//===- Package.cpp - Package a DirectX demo shader
//--------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

int main(int Argc, char **Argv) {
  if (Argc != 4) {
    errs() << "Usage: directx-package <shader.dxil> <manifest.json> "
              "<output.bin>\n";
    return 1;
  }
  auto Shader = MemoryBuffer::getFile(Argv[1], false);
  if (!Shader) {
    errs() << Argv[1] << ": " << Shader.getError().message() << '\n';
    return 1;
  }
  auto Manifest = MemoryBuffer::getFile(Argv[2]);
  if (!Manifest) {
    errs() << Argv[2] << ": " << Manifest.getError().message() << '\n';
    return 1;
  }
  auto JSON = json::parse((*Manifest)->getBuffer());
  if (!JSON) {
    errs() << Argv[2] << ": " << toString(JSON.takeError()) << '\n';
    return 1;
  }
  object::OffloadBinary::OffloadingImage Image;
  Image.TheImageKind = object::IMG_DXIL;
  Image.StringData["directx.manifest"] = (*Manifest)->getBuffer();
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
