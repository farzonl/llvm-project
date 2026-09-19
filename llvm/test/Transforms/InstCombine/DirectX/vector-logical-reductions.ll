; RUN: opt -mtriple=dxil-pc-shadermodel6.5-library -passes=instcombine -S %s | FileCheck %s

define i1 @reduce_or(<4 x i1> %value) {
; CHECK-LABEL: define i1 @reduce_or(
; CHECK-NEXT:    [[RESULT:%.*]] = call i1 @llvm.vector.reduce.or.v4i1(<4 x i1> [[VALUE:%.*]])
; CHECK-NEXT:    ret i1 [[RESULT]]
  %result = call i1 @llvm.vector.reduce.or.v4i1(<4 x i1> %value)
  ret i1 %result
}

define i1 @reduce_and(<3 x i1> %value) {
; CHECK-LABEL: define i1 @reduce_and(
; CHECK-NEXT:    [[RESULT:%.*]] = call i1 @llvm.vector.reduce.and.v3i1(<3 x i1> [[VALUE:%.*]])
; CHECK-NEXT:    ret i1 [[RESULT]]
  %result = call i1 @llvm.vector.reduce.and.v3i1(<3 x i1> %value)
  ret i1 %result
}

define i1 @reduce_or_v2i1(<2 x i1> %value) {
; CHECK-LABEL: define i1 @reduce_or_v2i1(
; CHECK-NEXT:    [[RESULT:%.*]] = call i1 @llvm.vector.reduce.or.v2i1(<2 x i1> [[VALUE:%.*]])
; CHECK-NEXT:    ret i1 [[RESULT]]
  %result = call i1 @llvm.vector.reduce.or.v2i1(<2 x i1> %value)
  ret i1 %result
}

define i1 @reduce_and_v17i1(<17 x i1> %value) {
; CHECK-LABEL: define i1 @reduce_and_v17i1(
; CHECK-NEXT:    [[RESULT:%.*]] = call i1 @llvm.vector.reduce.and.v17i1(<17 x i1> [[VALUE:%.*]])
; CHECK-NEXT:    ret i1 [[RESULT]]
  %result = call i1 @llvm.vector.reduce.and.v17i1(<17 x i1> %value)
  ret i1 %result
}