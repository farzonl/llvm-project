
// HLSL Intrinsic Classes
//===- HLSLIntrinsics.td - Defines intrinsics for HLSL-*- tablegen ------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the intrinsics a backend needs to implement to 
//
//===----------------------------------------------------------------------===//
class ArchType<int arch> {
  int Arch = arch;
}
def dxil : ArchType<0>;
def spirv : ArchType<1>;

multiclass HLSLIntrinsics <ArchType Arch> {
    def _all : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_any_ty]>;
}
