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
