; RUN: not --crash llc --enable-directx-global-isel -O0 --mtriple=dxil-pc-shadermodel6.3-library -stop-after=legalizer -global-isel -verify-machineinstrs %s -o - 2>&1 | FileCheck %s

; CHECK: LLVM ERROR: unable to legalize instruction: %17:id(<4 x s32>) = G_INSERT_VECTOR_ELT %16:_, %12:_(s32), %11:_(s32) (in function: test_sin_vec4)

declare <4 x float> @llvm.sin.v4f32(<4 x float>)
define <4 x float>  @test_sin_vec4(<4 x float>  %x) #0 {
  %y = call <4 x float> @llvm.sin.v4f32(<4 x float> %x)
  ret <4 x float> %y
}

attributes #0 = { convergent norecurse nounwind "hlsl.export"}