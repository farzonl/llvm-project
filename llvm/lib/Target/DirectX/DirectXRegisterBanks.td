//===-- DirectXRegisterBanks.td - Describe SPIR-V RegBanks ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Note: InstructionSelector wants a RegClass so we will define one.

def IDRegBank : RegisterBank<"IDBank", [ID]>;
