//===--- DirectXCallLowering.cpp - Call lowering ------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the lowering of LLVM calls to machine code calls for
// GlobalISel.
//
//===----------------------------------------------------------------------===//

#include "DirectXCallLowering.h"
#include "DirectXTargetLowering.h"

using namespace llvm;

DirectXCallLowering::DirectXCallLowering(const DirectXTargetLowering &TLI)
    : CallLowering(&TLI) {}

bool DirectXCallLowering::lowerFormalArguments(
    MachineIRBuilder &MIRBuilder, const Function &F,
    ArrayRef<ArrayRef<Register>> VRegs, FunctionLoweringInfo &FLI) const {
  return true;
}

bool DirectXCallLowering::lowerReturn(MachineIRBuilder &MIRBuiler,
                                      const Value *Val,
                                      ArrayRef<Register> VRegs,
                                      FunctionLoweringInfo &FLI,
                                      Register SwiftErrorVReg) const {
  return true;
}

bool DirectXCallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                    CallLoweringInfo &Info) const {
  return true;
}