//===-- DirectXInstrInfo.td - Target Description for DirectX Target ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the DirectX instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "DirectXInstrFormats.td"

def AllocaDXILInst : PseudoDXILInst<(outs ID:$res), (ins i32imm:$a), "$res = alloca align $a">;

let isReturn = 1, hasDelaySlot = 0, isBarrier = 0, isTerminator = 1, isNotDuplicable = 1 in {
    def ReturnDXILInst: SimplePseudoDXILInst<"ret void">;
    def ReturnValueDXILInst: PseudoDXILInst<(outs), (ins ID:$ret), "ret $ret">;
}

def SinFloat : UnaryDXILInst<13, FloatTy>;
def SinHalf : UnaryDXILInst<13, HalfTy>;
def CosFloat : UnaryDXILInst<12, FloatTy>;
def CosHalf : UnaryDXILInst<12, HalfTy>;
