; RUN: opt -S -scalarizer -dxil-op-lower -mtriple=dxil-pc-shadermodel6.3-library %s | FileCheck %s

; This test exercises the lowering of the intrinsic @llvm.usub.with.overflow.i32 to the USubb DXIL op

; CHECK-DAG: [[DX_TYPES_I32C:%dx\.types\.i32c]] = type { i32, i1 }

; NOTE: The uint2 overload of a SubUint64-style HLSL function uses @llvm.usub.with.overflow.i32, resulting in one USubb op
define noundef i32 @test_USubb(i32 noundef %a, i32 noundef %b) {
; CHECK-LABEL: define noundef i32 @test_USubb(
; CHECK-SAME: i32 noundef [[A:%.*]], i32 noundef [[B:%.*]]) {
; CHECK-NEXT:    [[USUBB:%.*]] = call [[DX_TYPES_I32C]] @dx.op.binaryWithCarryOrBorrow.i32(i32 45, i32 [[A]], i32 [[B]])
; CHECK-NEXT:    [[BORROW:%.*]] = extractvalue [[DX_TYPES_I32C]] [[USUBB]], 1
; CHECK-NEXT:    [[DIFF:%.*]] = extractvalue [[DX_TYPES_I32C]] [[USUBB]], 0
; CHECK-NEXT:    [[BORROW_ZEXT:%.*]] = zext i1 [[BORROW]] to i32
; CHECK-NEXT:    [[RESULT:%.*]] = sub i32 [[DIFF]], [[BORROW_ZEXT]]
; CHECK-NEXT:    ret i32 [[RESULT]]
;
  %usubb = call { i32, i1 } @llvm.usub.with.overflow.i32(i32 %a, i32 %b)
  %borrow = extractvalue { i32, i1 } %usubb, 1
  %diff = extractvalue { i32, i1 } %usubb, 0
  %borrow_zext = zext i1 %borrow to i32
  %result = sub i32 %diff, %borrow_zext
  ret i32 %result
}

; NOTE: The uint4 overload of a SubUint64-style HLSL function uses @llvm.usub.with.overflow.v2i32, resulting in two USubb ops after scalarization
define noundef <2 x i32> @test_USubb_vec2(<2 x i32> noundef %a, <2 x i32> noundef %b) {
; CHECK-LABEL: define noundef <2 x i32> @test_USubb_vec2(
; CHECK-SAME: <2 x i32> noundef [[A:%.*]], <2 x i32> noundef [[B:%.*]]) {
; CHECK-NEXT:    [[A_I0:%.*]] = extractelement <2 x i32> [[A]], i64 0
; CHECK-NEXT:    [[B_I0:%.*]] = extractelement <2 x i32> [[B]], i64 0
; CHECK-NEXT:    [[USUBB_I0:%.*]] = call [[DX_TYPES_I32C]] @dx.op.binaryWithCarryOrBorrow.i32(i32 45, i32 [[A_I0]], i32 [[B_I0]])
; CHECK-NEXT:    [[A_I1:%.*]] = extractelement <2 x i32> [[A]], i64 1
; CHECK-NEXT:    [[B_I1:%.*]] = extractelement <2 x i32> [[B]], i64 1
; CHECK-NEXT:    [[USUBB_I1:%.*]] = call [[DX_TYPES_I32C]] @dx.op.binaryWithCarryOrBorrow.i32(i32 45, i32 [[A_I1]], i32 [[B_I1]])
; CHECK-NEXT:    [[BORROW_ELEM0:%.*]] = extractvalue [[DX_TYPES_I32C]] [[USUBB_I0]], 1
; CHECK-NEXT:    [[BORROW_ELEM1:%.*]] = extractvalue [[DX_TYPES_I32C]] [[USUBB_I1]], 1
; CHECK-NEXT:    [[DIFF_ELEM0:%.*]] = extractvalue [[DX_TYPES_I32C]] [[USUBB_I0]], 0
; CHECK-NEXT:    [[DIFF_ELEM1:%.*]] = extractvalue [[DX_TYPES_I32C]] [[USUBB_I1]], 0
; CHECK-NEXT:    [[BORROW_ZEXT_I0:%.*]] = zext i1 [[BORROW_ELEM0]] to i32
; CHECK-NEXT:    [[BORROW_ZEXT_I1:%.*]] = zext i1 [[BORROW_ELEM1]] to i32
; CHECK-NEXT:    [[RESULT_I0:%.*]] = sub i32 [[DIFF_ELEM0]], [[BORROW_ZEXT_I0]]
; CHECK-NEXT:    [[RESULT_I1:%.*]] = sub i32 [[DIFF_ELEM1]], [[BORROW_ZEXT_I1]]
; CHECK-NEXT:    [[RESULT_UPTO0:%.*]] = insertelement <2 x i32> poison, i32 [[RESULT_I0]], i64 0
; CHECK-NEXT:    [[RESULT:%.*]] = insertelement <2 x i32> [[RESULT_UPTO0]], i32 [[RESULT_I1]], i64 1
; CHECK-NEXT:    ret <2 x i32> [[RESULT]]
;
  %usubb = call { <2 x i32>, <2 x i1> } @llvm.usub.with.overflow.v2i32(<2 x i32> %a, <2 x i32> %b)
  %borrow = extractvalue { <2 x i32>, <2 x i1> } %usubb, 1
  %diff = extractvalue { <2 x i32>, <2 x i1> } %usubb, 0
  %borrow_zext = zext <2 x i1> %borrow to <2 x i32>
  %result = sub <2 x i32> %diff, %borrow_zext
  ret <2 x i32> %result
}

define noundef i32 @test_USubb_insert(i32 noundef %a, i32 noundef %b) {
; CHECK-LABEL: define noundef i32 @test_USubb_insert(
; CHECK-SAME: i32 noundef [[A:%.*]], i32 noundef [[B:%.*]]) {
; CHECK-NEXT:    [[USUBB:%.*]] = call [[DX_TYPES_I32C]] @dx.op.binaryWithCarryOrBorrow.i32(i32 45, i32 [[A]], i32 [[B]])
; CHECK-NEXT:    [[UNUSED:%.*]] = insertvalue [[DX_TYPES_I32C]] [[USUBB]], i32 [[A]], 0
; CHECK-NEXT:    [[RESULT:%.*]] = extractvalue [[DX_TYPES_I32C]] [[USUBB]], 0
; CHECK-NEXT:    ret i32 [[RESULT]]
;
  %usubb = call { i32, i1 } @llvm.usub.with.overflow.i32(i32 %a, i32 %b)
  insertvalue { i32, i1 } %usubb, i32 %a, 0
  %result = extractvalue { i32, i1 } %usubb, 0
  ret i32 %result
}

; CHECK: declare [[DX_TYPES_I32C]] @dx.op.binaryWithCarryOrBorrow.i32(i32, i32, i32) #[[#ATTR0:]]
; CHECK: attributes #[[#ATTR0]] = { nounwind memory(none) }

declare { i32, i1 } @llvm.usub.with.overflow.i32(i32, i32)
