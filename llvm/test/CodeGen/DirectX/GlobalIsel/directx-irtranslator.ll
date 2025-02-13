; RUN: llc --enable-directx-global-isel -O0 --mtriple=dxil-pc-shadermodel6.3-library -stop-after=irtranslator -global-isel -verify-machineinstrs %s -o - 2>&1 | FileCheck %s
; RUN: llc --enable-directx-global-isel -O0 --mtriple=dxil-pc-shadermodel6.3-library -stop-after=legalizer -global-isel -verify-machineinstrs %s -o - 2>&1 | FileCheck %s

define void @test_void_return() #0 {
  ; CHECK-LABEL: name: test_void_return
  ; CHECK: ReturnDXILInst
  ret void
}

define float @test_float_return() #0 {
  ; CHECK-LABEL: name: test_float_return
  ; CHECK: G_FCONSTANT float 0.000000e+00
  ; CHECK: ReturnValueDXILInst %0(s32)
  ret float 0.000000e+00
}

define float @test_float_return_arg(float %x) #0 {
  ; CHECK-LABEL: name: test_float_return_arg
  ; CHECK: bb.1 (%ir-block.0):
  ; CHECK-NEXT: %0:id(s32) = AllocaDXILInst 4
  ; CHECK-NEXT: ReturnValueDXILInst %0(s32)
  ret float %x
}

define i32 @test_i32_add_args(i32 %x, i32 %y) #0 {
  ; CHECK-LABEL: name: test_i32_add_args
  ; CHECK: bb.1 (%ir-block.0):
  ; CHECK-NEXT: %0:id(s32) = AllocaDXILInst 4
  ; CHECK-NEXT: %1:id(s32) = AllocaDXILInst 4
  ; CHECK-NEXT: %2:id(s32) = G_ADD %0, %1
  ; CHECK-NEXT: ReturnValueDXILInst %2(s32)
  %z = add i32 %x, %y
  ret i32 %z
}

declare float @llvm.cos.f32(float)
define float @test_cos_f32(float %x) #0 {
  ; CHECK-LABEL: name: test_cos_f32
  ; CHECK: bb.1 (%ir-block.0):
  ; CHECK-NEXT: %0:id(s32) = AllocaDXILInst 4
  ; CHECK-NEXT: %{{[0-9]+}}:id(s32) = G_FCOS %{{[0-9]+}}
  ; CHECK-NEXT: ReturnValueDXILInst %1(s32)
  %y = call float @llvm.cos.f32(float %x)
  ret float %y
}

declare float @llvm.sin.f32(float)
define float @test_sin_f32(float %x) #0 {
  ; CHECK-LABEL: name: test_sin_f32
  ; CHECK: bb.1 (%ir-block.0):
  ; CHECK-NEXT: %0:id(s32) = AllocaDXILInst 4
  ; CHECK-NEXT: %{{[0-9]+}}:id(s32) = G_FSIN %{{[0-9]+}}
  ; CHECK-NEXT: ReturnValueDXILInst %1(s32)
  %y = call float @llvm.sin.f32(float %x)
  ret float %y
}

attributes #0 = { convergent norecurse nounwind "hlsl.export"}
