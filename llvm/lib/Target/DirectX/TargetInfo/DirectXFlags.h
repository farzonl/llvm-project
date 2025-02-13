//===- DirectXTargetMachine.h - DirectX Target Flags ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLine.h"

namespace llvm {

extern llvm::cl::opt<bool> EnableDirectXGlobalIsel;
extern llvm::cl::opt<bool> EnableDirectXGlobalIselASMPrinter;
} // namespace llvm
