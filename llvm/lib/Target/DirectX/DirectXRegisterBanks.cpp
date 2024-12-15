//===- DirectXRegisterBankInfo.cpp ------------------------------*- C++
//-*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the targeting of the RegisterBankInfo class for SPIR-V.
//
//===----------------------------------------------------------------------===//

#include "DirectXRegisterBankInfo.h"
#include "DirectXRegisterInfo.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/RegisterBank.h"

#define GET_REGINFO_ENUM
#include "DirectXGenRegisterInfo.inc"

#define GET_TARGET_REGBANK_IMPL
#include "DirectXGenRegisterBank.inc"

using namespace llvm;

// This required for .td selection patterns to work or we'd end up with RegClass
// checks being redundant as all the classes would be mapped to the same bank.
const RegisterBank &
DirectXRegisterBankInfo::getRegBankFromRegClass(const TargetRegisterClass &RC,
                                                LLT Ty) const {
  switch (RC.getID()) {
  case dxil::IDRegClassID:
    return DirectX::IDRegBank;
  default:
    return DirectXGenRegisterBankInfo::getRegBankFromRegClass(RC, Ty);
  }
}
