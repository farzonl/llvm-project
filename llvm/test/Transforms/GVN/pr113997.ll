; NOTE: Assertions have been autogenerated by utils/update_test_checks.py UTC_ARGS: --version 5
; RUN: opt -S -passes=gvn < %s | FileCheck %s

; Make sure attributes in function calls are intersected correctly.

define i1 @bucket(i32 noundef %x) {
; CHECK-LABEL: define i1 @bucket(
; CHECK-SAME: i32 noundef [[X:%.*]]) {
; CHECK-NEXT:    [[CMP1:%.*]] = icmp sgt i32 [[X]], 0
; CHECK-NEXT:    [[CTPOP1:%.*]] = tail call range(i32 0, 33) i32 @llvm.ctpop.i32(i32 [[X]])
; CHECK-NEXT:    [[CMP2:%.*]] = icmp samesign ult i32 [[CTPOP1]], 2
; CHECK-NEXT:    [[COND:%.*]] = select i1 [[CMP1]], i1 [[CMP2]], i1 false
; CHECK-NEXT:    br i1 [[COND]], label %[[IF_THEN:.*]], label %[[IF_ELSE:.*]]
; CHECK:       [[IF_ELSE]]:
; CHECK-NEXT:    [[RES:%.*]] = icmp eq i32 [[CTPOP1]], 1
; CHECK-NEXT:    ret i1 [[RES]]
; CHECK:       [[IF_THEN]]:
; CHECK-NEXT:    ret i1 false
;
  %cmp1 = icmp sgt i32 %x, 0
  %ctpop1 = tail call range(i32 1, 32) i32 @llvm.ctpop.i32(i32 %x)
  %cmp2 = icmp samesign ult i32 %ctpop1, 2
  %cond = select i1 %cmp1, i1 %cmp2, i1 false
  br i1 %cond, label %if.then, label %if.else

if.else:
  %ctpop2 = tail call range(i32 0, 33) i32 @llvm.ctpop.i32(i32 %x)
  %res = icmp eq i32 %ctpop2, 1
  ret i1 %res

if.then:
  ret i1 false
}

; Make sure we don't merge these two users of the incompatible call pair.

define i1 @bucket2(i32 noundef %x) {
; CHECK-LABEL: define i1 @bucket2(
; CHECK-SAME: i32 noundef [[X:%.*]]) {
; CHECK-NEXT:    [[CMP1:%.*]] = icmp sgt i32 [[X]], 0
; CHECK-NEXT:    [[CTPOP1:%.*]] = tail call range(i32 1, 32) i32 @llvm.ctpop.i32(i32 zeroext [[X]])
; CHECK-NEXT:    [[CTPOP1INC:%.*]] = add i32 [[CTPOP1]], 1
; CHECK-NEXT:    [[CMP2:%.*]] = icmp samesign ult i32 [[CTPOP1INC]], 3
; CHECK-NEXT:    [[COND:%.*]] = select i1 [[CMP1]], i1 [[CMP2]], i1 false
; CHECK-NEXT:    br i1 [[COND]], label %[[IF_THEN:.*]], label %[[IF_ELSE:.*]]
; CHECK:       [[IF_ELSE]]:
; CHECK-NEXT:    [[CTPOP2:%.*]] = tail call range(i32 0, 33) i32 @llvm.ctpop.i32(i32 [[X]])
; CHECK-NEXT:    [[CTPOP2INC:%.*]] = add i32 [[CTPOP2]], 1
; CHECK-NEXT:    [[RES:%.*]] = icmp eq i32 [[CTPOP2INC]], 2
; CHECK-NEXT:    ret i1 [[RES]]
; CHECK:       [[IF_THEN]]:
; CHECK-NEXT:    ret i1 false
;
  %cmp1 = icmp sgt i32 %x, 0
  %ctpop1 = tail call range(i32 1, 32) i32 @llvm.ctpop.i32(i32 zeroext %x)
  %ctpop1inc = add i32 %ctpop1, 1
  %cmp2 = icmp samesign ult i32 %ctpop1inc, 3
  %cond = select i1 %cmp1, i1 %cmp2, i1 false
  br i1 %cond, label %if.then, label %if.else

if.else:
  %ctpop2 = tail call range(i32 0, 33) i32 @llvm.ctpop.i32(i32 %x)
  %ctpop2inc = add i32 %ctpop2, 1
  %res = icmp eq i32 %ctpop2inc, 2
  ret i1 %res

if.then:
  ret i1 false
}
