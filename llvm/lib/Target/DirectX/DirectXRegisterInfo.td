//===-- DirectXRegisterInfo.td - DirectX Register defs --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Declarations that describe the DirectX register file.
//
//===----------------------------------------------------------------------===//

//let Namespace = "dxil" in {
//  def DXIL : Register<"dxil">;
//  def ID : RegisterClass<"dxil", [i32], 32, (add DXIL)>;
//}

let Namespace = "dxil" in {
    
    // // Class for type registers
    // def TYPE0 : Register<"TYPE0">;
    // def TYPE : RegisterClass<"dxil", [i32], 32, (add TYPE0)>;

    def ID0 : Register<"ID0">;
    def fID0 : Register<"fID0">;
    def iID : RegisterClass<"dxil", [i32], 32, (add ID0)>;
    def fID : RegisterClass<"dxil", [f32], 32, (add fID0)>;

    def ID : RegisterClass<
      "dxil",
      [i32, f32],
      32,
      (add iID, fID)>;
}
