; RUN: llc --enable-directx-global-isel -O0 --mtriple=dxil-pc-shadermodel6.3-library -stop-after=irtranslator -global-isel -verify-machineinstrs %s -o - 2>&1 | FileCheck %s

define void @test_void_return() #0 {
  ; CHECK-LABEL: name: test_void_return
  ; CHECK: ReturnDXILInst
  ret void
}

define float @test_float_return() #0 {
  ; CHECK-LABEL: name: test_float_return
  ; CHECK: G_FCONSTANT float 0.000000e+00
  ; CHECK: ReturnValueDXILInst %0(s32), <{{.*}}>
  ret float 0.000000e+00
}

;declare float @llvm.cos.f32(float)
;define float @test_cos_f32(float %x) #0 {
;  ; IGNORE-LABEL: name: test_cos_f32
;  ; IGNORE: %{{[0-9]+}}:_(s32) = G_FCOS %{{[0-9]+}}
;  %y = call float @llvm.cos.f32(float %x)
;  ret float %y
;}

;declare float @llvm.sin.f32(float)
;define float @test_sin_f32(float %x) #0 {
;  ; IGNORE-LABEL: name: test_sin_f32
;  ; IGNORE: %{{[0-9]+}}:_(s32) = G_FSIN %{{[0-9]+}}
;  %y = call float @llvm.sin.f32(float %x)
;  ret float %y
;}

attributes #0 = { convergent norecurse nounwind "hlsl.export"}