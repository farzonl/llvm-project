; RUN: opt -S -scalarizer -dxil-op-lower -mtriple=dxil-pc-shadermodel6.3-library %s | FileCheck %s

; This test exercises the lowering of the intrinsic @llvm.smul.with.overflow.i32
; to the IMul DXIL op. IMul returns the full 64-bit product split into its low
; and high 32-bit halves (%dx.types.twoi32 = { i32, i32 }), so the overflow bit
; is synthesized as (high != (low >> 31)).

; CHECK-DAG: [[DX_TYPES_TWOI32:%dx\.types\.twoi32]] = type { i32, i32 }

define noundef i32 @test_IMul(i32 noundef %a, i32 noundef %b) {
; CHECK-LABEL: define noundef i32 @test_IMul(
; CHECK-SAME: i32 noundef [[A:%.*]], i32 noundef [[B:%.*]]) {
; CHECK-NEXT:    [[IMUL:%.*]] = call [[DX_TYPES_TWOI32]] @dx.op.binaryWithTwoOuts.i32(i32 41, i32 [[A]], i32 [[B]])
; CHECK-NEXT:    [[LOW:%.*]] = extractvalue [[DX_TYPES_TWOI32]] [[IMUL]], 0
; CHECK-NEXT:    [[HIGH:%.*]] = extractvalue [[DX_TYPES_TWOI32]] [[IMUL]], 1
; CHECK-NEXT:    [[SIGNBITS:%.*]] = ashr i32 [[LOW]], 31
; CHECK-NEXT:    [[OVF_BIT:%.*]] = icmp ne i32 [[HIGH]], [[SIGNBITS]]
; CHECK-NEXT:    [[AGG0:%.*]] = insertvalue { i32, i1 } poison, i32 [[LOW]], 0
; CHECK-NEXT:    [[AGG1:%.*]] = insertvalue { i32, i1 } [[AGG0]], i1 [[OVF_BIT]], 1
; CHECK-NEXT:    [[PROD:%.*]] = extractvalue { i32, i1 } [[AGG1]], 0
; CHECK-NEXT:    [[OVF:%.*]] = extractvalue { i32, i1 } [[AGG1]], 1
; CHECK-NEXT:    [[OVF_ZEXT:%.*]] = zext i1 [[OVF]] to i32
; CHECK-NEXT:    [[RESULT:%.*]] = add i32 [[PROD]], [[OVF_ZEXT]]
; CHECK-NEXT:    ret i32 [[RESULT]]
;
  %imul = call { i32, i1 } @llvm.smul.with.overflow.i32(i32 %a, i32 %b)
  %prod = extractvalue { i32, i1 } %imul, 0
  %ovf = extractvalue { i32, i1 } %imul, 1
  %ovf_zext = zext i1 %ovf to i32
  %result = add i32 %prod, %ovf_zext
  ret i32 %result
}

define noundef <2 x i32> @test_IMul_vec2(<2 x i32> noundef %a, <2 x i32> noundef %b) {
; CHECK-LABEL: define noundef <2 x i32> @test_IMul_vec2(
; CHECK-SAME: <2 x i32> noundef [[A:%.*]], <2 x i32> noundef [[B:%.*]]) {
; CHECK-NEXT:    [[A_I0:%.*]] = extractelement <2 x i32> [[A]], i64 0
; CHECK-NEXT:    [[B_I0:%.*]] = extractelement <2 x i32> [[B]], i64 0
; CHECK-NEXT:    [[IMUL_I0:%.*]] = call [[DX_TYPES_TWOI32]] @dx.op.binaryWithTwoOuts.i32(i32 41, i32 [[A_I0]], i32 [[B_I0]])
; CHECK-NEXT:    [[LOW_I0:%.*]] = extractvalue [[DX_TYPES_TWOI32]] [[IMUL_I0]], 0
; CHECK-NEXT:    [[HIGH_I0:%.*]] = extractvalue [[DX_TYPES_TWOI32]] [[IMUL_I0]], 1
; CHECK-NEXT:    [[SIGNBITS_I0:%.*]] = ashr i32 [[LOW_I0]], 31
; CHECK-NEXT:    [[OVF_BIT_I0:%.*]] = icmp ne i32 [[HIGH_I0]], [[SIGNBITS_I0]]
; CHECK-NEXT:    [[AGG0_I0:%.*]] = insertvalue { i32, i1 } poison, i32 [[LOW_I0]], 0
; CHECK-NEXT:    [[AGG1_I0:%.*]] = insertvalue { i32, i1 } [[AGG0_I0]], i1 [[OVF_BIT_I0]], 1
; CHECK-NEXT:    [[A_I1:%.*]] = extractelement <2 x i32> [[A]], i64 1
; CHECK-NEXT:    [[B_I1:%.*]] = extractelement <2 x i32> [[B]], i64 1
; CHECK-NEXT:    [[IMUL_I1:%.*]] = call [[DX_TYPES_TWOI32]] @dx.op.binaryWithTwoOuts.i32(i32 41, i32 [[A_I1]], i32 [[B_I1]])
; CHECK-NEXT:    [[LOW_I1:%.*]] = extractvalue [[DX_TYPES_TWOI32]] [[IMUL_I1]], 0
; CHECK-NEXT:    [[HIGH_I1:%.*]] = extractvalue [[DX_TYPES_TWOI32]] [[IMUL_I1]], 1
; CHECK-NEXT:    [[SIGNBITS_I1:%.*]] = ashr i32 [[LOW_I1]], 31
; CHECK-NEXT:    [[OVF_BIT_I1:%.*]] = icmp ne i32 [[HIGH_I1]], [[SIGNBITS_I1]]
; CHECK-NEXT:    [[AGG0_I1:%.*]] = insertvalue { i32, i1 } poison, i32 [[LOW_I1]], 0
; CHECK-NEXT:    [[AGG1_I1:%.*]] = insertvalue { i32, i1 } [[AGG0_I1]], i1 [[OVF_BIT_I1]], 1
; CHECK-NEXT:    [[PROD_ELEM0:%.*]] = extractvalue { i32, i1 } [[AGG1_I0]], 0
; CHECK-NEXT:    [[PROD_ELEM1:%.*]] = extractvalue { i32, i1 } [[AGG1_I1]], 0
; CHECK-NEXT:    [[OVF_ELEM0:%.*]] = extractvalue { i32, i1 } [[AGG1_I0]], 1
; CHECK-NEXT:    [[OVF_ELEM1:%.*]] = extractvalue { i32, i1 } [[AGG1_I1]], 1
; CHECK-NEXT:    [[OVF_ZEXT_I0:%.*]] = zext i1 [[OVF_ELEM0]] to i32
; CHECK-NEXT:    [[OVF_ZEXT_I1:%.*]] = zext i1 [[OVF_ELEM1]] to i32
; CHECK-NEXT:    [[RESULT_I0:%.*]] = add i32 [[PROD_ELEM0]], [[OVF_ZEXT_I0]]
; CHECK-NEXT:    [[RESULT_I1:%.*]] = add i32 [[PROD_ELEM1]], [[OVF_ZEXT_I1]]
; CHECK-NEXT:    [[RESULT_UPTO0:%.*]] = insertelement <2 x i32> poison, i32 [[RESULT_I0]], i64 0
; CHECK-NEXT:    [[RESULT:%.*]] = insertelement <2 x i32> [[RESULT_UPTO0]], i32 [[RESULT_I1]], i64 1
; CHECK-NEXT:    ret <2 x i32> [[RESULT]]
;
  %imul = call { <2 x i32>, <2 x i1> } @llvm.smul.with.overflow.v2i32(<2 x i32> %a, <2 x i32> %b)
  %prod = extractvalue { <2 x i32>, <2 x i1> } %imul, 0
  %ovf = extractvalue { <2 x i32>, <2 x i1> } %imul, 1
  %ovf_zext = zext <2 x i1> %ovf to <2 x i32>
  %result = add <2 x i32> %prod, %ovf_zext
  ret <2 x i32> %result
}

; CHECK: declare [[DX_TYPES_TWOI32]] @dx.op.binaryWithTwoOuts.i32(i32, i32, i32) #[[#ATTR0:]]
; CHECK: attributes #[[#ATTR0]] = { nounwind memory(none) }

declare { i32, i1 } @llvm.smul.with.overflow.i32(i32, i32)
declare { <2 x i32>, <2 x i1> } @llvm.smul.with.overflow.v2i32(<2 x i32>, <2 x i32>)
