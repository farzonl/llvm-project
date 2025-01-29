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


def ReturnDXILInst: SimplePseudoDXILInst<"ret void">;
//def ReturnValueDXILInst: PseudoDXILInst<(outs), (ins ID:$ret, TYPE:$type), "ret $type $ret">;
def ReturnValueDXILInst: PseudoDXILInst<(outs), (ins ID:$ret), "ret $ret">;
