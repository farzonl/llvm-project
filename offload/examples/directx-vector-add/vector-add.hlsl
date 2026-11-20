// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

ByteAddressBuffer A : register(t0);
ByteAddressBuffer B : register(t1);
RWByteAddressBuffer C : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 ID : SV_DispatchThreadID) {
  uint Bytes;
  A.GetDimensions(Bytes);
  if (ID.x < Bytes / 4) {
    uint Offset = ID.x * 4;
    C.Store(Offset, asuint(asfloat(A.Load(Offset)) + asfloat(B.Load(Offset))));
  }
}
