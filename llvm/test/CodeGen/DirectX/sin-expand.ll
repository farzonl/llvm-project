; RUN: opt -S -dxil-elementwise-expansion < %s | FileCheck %s

; Make sure dxil operation function calls for sin are generated for float and half.
; CHECK:extractelement <{{.*}} x float> %{{.*}}, i32 {{.*}}
; CHECK:call float @llvm.sin.f32(float %{{.*}})
; CHECK:insertelement <{{.*}} x float>
target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.3-library"

; Function Attrs: noinline nounwind optnone
define noundef <2 x float> @"?test_sin_float2@@YAT?$__vector@M$01@__clang@@T12@@Z"(<2 x float> noundef %p0) #0 {
entry:
  %p0.addr = alloca <2 x float>, align 8
  store <2 x float> %p0, ptr %p0.addr, align 8
  %0 = load <2 x float>, ptr %p0.addr, align 8
  %elt.sin = call <2 x float> @llvm.sin.v2f32(<2 x float> %0)
  ret <2 x float> %elt.sin
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare <2 x float> @llvm.sin.v2f32(<2 x float>) #1

; Function Attrs: noinline nounwind optnone
define noundef <3 x float> @"?test_sin_float3@@YAT?$__vector@M$02@__clang@@T12@@Z"(<3 x float> noundef %p0) #0 {
entry:
  %p0.addr = alloca <3 x float>, align 16
  store <3 x float> %p0, ptr %p0.addr, align 16
  %0 = load <3 x float>, ptr %p0.addr, align 16
  %elt.sin = call <3 x float> @llvm.sin.v3f32(<3 x float> %0)
  ret <3 x float> %elt.sin
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare <3 x float> @llvm.sin.v3f32(<3 x float>) #1

; Function Attrs: noinline nounwind optnone
define noundef <4 x float> @"?test_sin_float4@@YAT?$__vector@M$03@__clang@@T12@@Z"(<4 x float> noundef %p0) #0 {
entry:
  %p0.addr = alloca <4 x float>, align 16
  store <4 x float> %p0, ptr %p0.addr, align 16
  %0 = load <4 x float>, ptr %p0.addr, align 16
  %elt.sin = call <4 x float> @llvm.sin.v4f32(<4 x float> %0)
  ret <4 x float> %elt.sin
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare <4 x float> @llvm.sin.v4f32(<4 x float>) #1

attributes #0 = { noinline nounwind optnone "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 4, !"dx.disable_optimizations", i32 1}
!2 = !{!"clang version 19.0.0git (https://github.com/llvm/llvm-project.git cff36bb198759c4fe557adc594eabc097cf7d565)"}
