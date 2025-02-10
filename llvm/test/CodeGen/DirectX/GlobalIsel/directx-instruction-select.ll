; RUN: llc --enable-directx-global-isel -O0 --mtriple=dxil-pc-shadermodel6.3-library -stop-after=instruction-select -global-isel -verify-machineinstrs %s -o - 2>&1 | FileCheck %s

declare float @llvm.cos.f32(float)
define float @test_cos_f32(float %x) #0 {
  ; CHECK-LABEL: name: test_cos_f32
  ; CHECK: bb.1 (%ir-block.0):
  ; CHECK-NEXT: %0:id = AllocaDXILInst 2, 4, <{{.*}}>
  ; CHECK-NEXT: %1:id = CosDXILInst %0
  ; CHECK-NEXT: ReturnValueDXILInst 2, %1, <{{.*}}>
  %y = call float @llvm.cos.f32(float %x)
  ret float %y
}

declare float @llvm.sin.f32(float)
define float @test_sin_f32(float %x) #0 {
  ; CHECK-LABEL: name: test_sin_f32
  ; CHECK: bb.1 (%ir-block.0):
  ; CHECK-NEXT: %0:id = AllocaDXILInst 2, 4, <{{.*}}>
  ; CHECK-NEXT: %1:id = SinDXILInst %0
  ; CHECK-NEXT: ReturnValueDXILInst 2, %1, <{{.*}}>
  %y = call float @llvm.sin.f32(float %x)
  ret float %y
}

attributes #0 = { convergent norecurse nounwind "hlsl.export"}
