// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Regenerate manifest.dxil with DXC v1.9.2609:
// dxc -T cs_6_0 -E main -O3 -Fo manifest.dxil manifest.hlsl
// dxc -T cs_6_0 -E main -O3 -DNO_CONSTANTS -Fo buffers.dxil manifest.hlsl

ByteAddressBuffer Input : register(t2, space1);
RWByteAddressBuffer Output : register(u3, space1);
cbuffer Parameters : register(b4, space2) {
  uint Count;
  uint64_t Bias;
};

[numthreads(8, 2, 1)]
void main(uint3 ID : SV_DispatchThreadID) {
#ifdef NO_CONSTANTS
  Output.Store(4 * ID.x, Input.Load(4 * ID.x));
#else
  if (ID.x < Count)
    Output.Store(4 * ID.x, Input.Load(4 * ID.x) + uint(Bias + ID.x));
#endif
}
