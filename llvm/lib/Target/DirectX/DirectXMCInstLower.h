//=- DirectXMCInstLower.h -- Convert DX MachineInstr to MCInst ----*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_DIRECTX_DIRECTXMCINSTLOWER_H
#define LLVM_LIB_TARGET_DIRECTX_DIRECTXMCINSTLOWER_H

#include "llvm/Support/Compiler.h"

namespace llvm {
class MCInst;
class MachineInstr;
namespace dxil {
struct ModuleAnalysisInfo;
} // namespace dxil

// This class is used to lower a MachineInstr into an MCInst.
class LLVM_LIBRARY_VISIBILITY DirectXMCInstLower {
public:
  void lower(const MachineInstr *MI, MCInst &OutMI,
             dxil::ModuleAnalysisInfo *MAI) const;
};
} // namespace llvm

#endif // LLVM_LIB_TARGET_DIRECTX_DIRECTXMCINSTLOWER_H
