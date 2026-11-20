// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// clang --target=dxil-pc-shadermodel6.0-compute -x hlsl -O3 -c \
//   groupshared.hlsl -o groupshared.dxil
// dxv -o=groupshared.dxil groupshared.dxil

ByteAddressBuffer Input : register(t2, space1);
RWByteAddressBuffer Output : register(u3, space1);
groupshared uint Values[128];

[numthreads(128, 1, 1)]
void main(uint3 Group : SV_GroupID, uint Index : SV_GroupIndex) {
  uint Base = Group.x * 128;
  Values[Index] = Input.Load(4 * (Base + Index));
  GroupMemoryBarrierWithGroupSync();
  uint Sum = 0;
  for (uint I = 0; I < 128; ++I)
    Sum += Values[I];
  GroupMemoryBarrierWithGroupSync();
  Values[Index] = Sum + Index;
  GroupMemoryBarrierWithGroupSync();
  Output.Store(4 * (Base + Index), Values[127 - Index]);
}
