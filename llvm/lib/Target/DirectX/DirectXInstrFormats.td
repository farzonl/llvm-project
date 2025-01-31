//===-- DirectXInstrFormats.td - DirectX Instruction Formats --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def StringImm: Operand<i32>{
  let PrintMethod="printStringImm";
}

class DXILInst<bits<16> Opcode, dag outs, dag ins, string asmstr, list<dag> pattern = []>
  : Instruction {
  field bits<16> Inst;

  let Inst = Opcode;

  let Namespace = "dxil";
  let DecoderNamespace = "dxil";

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
}

// Pseudo instructions
class PseudoDXILInst<dag outs, dag ins, string asmstr> :  DXILInst<0, outs, ins, asmstr> {
  let isPseudo = 1;
  let hasSideEffects = 0;
}

//class UnaryDXILInst<bits<16> Opcode>:  
//  DXILInst<Opcode, 
//          (outs ID:$res), 
//          (ins ID:$arg, i32imm:$typeEnum), 
//          "call $typeEnum @dx.op.unary.f32(i32 $Opcode, $typeEnum $arg)">{}

class SimplePseudoDXILInst<string name>: PseudoDXILInst<(outs), (ins), name>;
