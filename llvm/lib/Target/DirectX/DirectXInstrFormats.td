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

class DXILType {
  int isOverload = 0;
  string TypeName = "";
  string TypeNameShort = "";
}

let isOverload = 1 in {
  def OverloadTy : DXILType;
}

def VoidTy : DXILType {
  let TypeName = "void";
  let TypeNameShort = "void";
}
def Int1Ty : DXILType;
def Int8Ty : DXILType;
def Int16Ty : DXILType {
  let TypeName = "i16";
  let TypeNameShort = "i16";
}
def Int32Ty : DXILType {
  let TypeName = "i32";
  let TypeNameShort = "i32";
}
def Int64Ty : DXILType {
  let TypeName = "i64";
  let TypeNameShort = "i64";
}

def HalfTy : DXILType {
  let TypeName = "half";
  let TypeNameShort = "f16";
}

def FloatTy : DXILType {
  let TypeName = "float";
  let TypeNameShort = "f32";
}
def DoubleTy : DXILType {
  let TypeName = "double";
  let TypeNameShort = "f64";
}

class Overloads<list<DXILType> oloads> {
  list<DXILType> overload_types = oloads;
}

class UnaryDXILInst<bits<16> Opcode>:  
  DXILInst<Opcode, 
           (outs ID:$res),           
           (ins ID:$arg), 
           "call #OverloadTy.TypeName# @dx.op.unary.#OverloadTy.TypeNameShort#(i32 $Opcode, #OverloadTy.TypeName# $arg)">
{
  list<DXILType> arguments = [];
  list<Overloads> overloads = [];
  DXILType result;
}

class SimplePseudoDXILInst<string name>: PseudoDXILInst<(outs), (ins), name>;
