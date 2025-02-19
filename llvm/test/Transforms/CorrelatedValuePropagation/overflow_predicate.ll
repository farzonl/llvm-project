; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt -S -passes=correlated-propagation < %s | FileCheck %s

declare void @llvm.trap()
declare {i8, i1} @llvm.uadd.with.overflow(i8, i8)
declare {i8, i1} @llvm.sadd.with.overflow(i8, i8)
declare {i8, i1} @llvm.usub.with.overflow(i8, i8)
declare {i8, i1} @llvm.ssub.with.overflow(i8, i8)
declare {i8, i1} @llvm.umul.with.overflow(i8, i8)
declare {i8, i1} @llvm.smul.with.overflow(i8, i8)

define i1 @uadd_ov_false(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @uadd_ov_false(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.uadd.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp ugt i8 [[X]], -102
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 false
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.uadd.with.overflow(i8 %x, i8 100)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %c1 = icmp ugt i8 %x, 154
  store i1 %c1, ptr %pc
  %c2 = icmp ugt i8 %x, 155
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @uadd_ov_true(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @uadd_ov_true(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.uadd.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[OVERFLOW:%.*]], label [[TRAP:%.*]]
; CHECK:       overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp samesign ugt i8 [[X]], -100
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.uadd.with.overflow(i8 %x, i8 100)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %overflow, label %trap

overflow:
  %c1 = icmp ugt i8 %x, 156
  store i1 %c1, ptr %pc
  %c2 = icmp ugt i8 %x, 155
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @sadd_ov_false(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @sadd_ov_false(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.sadd.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp sgt i8 [[X]], 26
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 false
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.sadd.with.overflow(i8 %x, i8 100)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %c1 = icmp sgt i8 %x, 26
  store i1 %c1, ptr %pc
  %c2 = icmp sgt i8 %x, 27
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @sadd_ov_true(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @sadd_ov_true(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.sadd.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[OVERFLOW:%.*]], label [[TRAP:%.*]]
; CHECK:       overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp samesign ugt i8 [[X]], 28
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.sadd.with.overflow(i8 %x, i8 100)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %overflow, label %trap

overflow:
  %c1 = icmp sgt i8 %x, 28
  store i1 %c1, ptr %pc
  %c2 = icmp sgt i8 %x, 27
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @usub_ov_false(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @usub_ov_false(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.usub.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp ult i8 [[X]], 101
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 false
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.usub.with.overflow(i8 %x, i8 100)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %c1 = icmp ult i8 %x, 101
  store i1 %c1, ptr %pc
  %c2 = icmp ult i8 %x, 100
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @usub_ov_true(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @usub_ov_true(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.usub.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[OVERFLOW:%.*]], label [[TRAP:%.*]]
; CHECK:       overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp samesign ult i8 [[X]], 99
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.usub.with.overflow(i8 %x, i8 100)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %overflow, label %trap

overflow:
  %c1 = icmp ult i8 %x, 99
  store i1 %c1, ptr %pc
  %c2 = icmp ult i8 %x, 100
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @ssub_ov_false(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @ssub_ov_false(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.ssub.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp slt i8 [[X]], -27
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 false
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.ssub.with.overflow(i8 %x, i8 100)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %c1 = icmp slt i8 %x, -27
  store i1 %c1, ptr %pc
  %c2 = icmp slt i8 %x, -28
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @ssub_ov_true(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @ssub_ov_true(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.ssub.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[OVERFLOW:%.*]], label [[TRAP:%.*]]
; CHECK:       overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp samesign ult i8 [[X]], -29
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.ssub.with.overflow(i8 %x, i8 100)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %overflow, label %trap

overflow:
  %c1 = icmp slt i8 %x, -29
  store i1 %c1, ptr %pc
  %c2 = icmp slt i8 %x, -28
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @umul_ov_false(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @umul_ov_false(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.umul.with.overflow.i8(i8 [[X:%.*]], i8 10)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp samesign ugt i8 [[X]], 24
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 false
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.umul.with.overflow(i8 %x, i8 10)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %c1 = icmp ugt i8 %x, 24
  store i1 %c1, ptr %pc
  %c2 = icmp ugt i8 %x, 25
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @umul_ov_true(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @umul_ov_true(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.umul.with.overflow.i8(i8 [[X:%.*]], i8 10)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[OVERFLOW:%.*]], label [[TRAP:%.*]]
; CHECK:       overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp ugt i8 [[X]], 26
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.umul.with.overflow(i8 %x, i8 10)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %overflow, label %trap

overflow:
  %c1 = icmp ugt i8 %x, 26
  store i1 %c1, ptr %pc
  %c2 = icmp ugt i8 %x, 25
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

; Signed mul is constrained from both sides.
define i1 @smul_ov_false_bound1(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @smul_ov_false_bound1(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.smul.with.overflow.i8(i8 [[X:%.*]], i8 10)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp slt i8 [[X]], -11
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 false
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.smul.with.overflow(i8 %x, i8 10)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %c1 = icmp slt i8 %x, -11
  store i1 %c1, ptr %pc
  %c2 = icmp slt i8 %x, -12
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @smul_ov_false_bound2(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @smul_ov_false_bound2(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.smul.with.overflow.i8(i8 [[X:%.*]], i8 10)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp sgt i8 [[X]], 11
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 false
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.smul.with.overflow(i8 %x, i8 10)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %c1 = icmp sgt i8 %x, 11
  store i1 %c1, ptr %pc
  %c2 = icmp sgt i8 %x, 12
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

; Can't use slt/sgt to test for a hole in the range, check equality instead.
define i1 @smul_ov_true_bound1(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @smul_ov_true_bound1(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.smul.with.overflow.i8(i8 [[X:%.*]], i8 10)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[OVERFLOW:%.*]], label [[TRAP:%.*]]
; CHECK:       overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp eq i8 [[X]], -13
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 false
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.smul.with.overflow(i8 %x, i8 10)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %overflow, label %trap

overflow:
  %c1 = icmp eq i8 %x, -13
  store i1 %c1, ptr %pc
  %c2 = icmp eq i8 %x, -12
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @smul_ov_true_bound2(i8 %x, ptr %px, ptr %pc) {
; CHECK-LABEL: @smul_ov_true_bound2(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.smul.with.overflow.i8(i8 [[X:%.*]], i8 10)
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    store i8 [[VAL]], ptr [[PX:%.*]], align 1
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[OVERFLOW:%.*]], label [[TRAP:%.*]]
; CHECK:       overflow:
; CHECK-NEXT:    [[C1:%.*]] = icmp eq i8 [[X]], 13
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 false
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.smul.with.overflow(i8 %x, i8 10)
  %val = extractvalue {i8, i1} %val_ov, 0
  store i8 %val, ptr %px
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %overflow, label %trap

overflow:
  %c1 = icmp eq i8 %x, 13
  store i1 %c1, ptr %pc
  %c2 = icmp eq i8 %x, 12
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @uadd_val(i8 %x, ptr %pc) {
; CHECK-LABEL: @uadd_val(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.uadd.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    [[C1:%.*]] = icmp ugt i8 [[VAL]], 100
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.uadd.with.overflow(i8 %x, i8 100)
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %val = extractvalue {i8, i1} %val_ov, 0
  %c1 = icmp ugt i8 %val, 100
  store i1 %c1, ptr %pc
  %c2 = icmp uge i8 %val, 100
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @sadd_val(i8 %x, ptr %pc) {
; CHECK-LABEL: @sadd_val(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.sadd.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    [[C1:%.*]] = icmp sgt i8 [[VAL]], -28
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.sadd.with.overflow(i8 %x, i8 100)
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %val = extractvalue {i8, i1} %val_ov, 0
  %c1 = icmp sgt i8 %val, -28
  store i1 %c1, ptr %pc
  %c2 = icmp sge i8 %val, -28
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @usub_val(i8 %x, ptr %pc) {
; CHECK-LABEL: @usub_val(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.usub.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    [[C1:%.*]] = icmp ult i8 [[VAL]], -101
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.usub.with.overflow(i8 %x, i8 100)
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %val = extractvalue {i8, i1} %val_ov, 0
  %c1 = icmp ult i8 %val, 155
  store i1 %c1, ptr %pc
  %c2 = icmp ule i8 %val, 155
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @ssub_val(i8 %x, ptr %pc) {
; CHECK-LABEL: @ssub_val(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.ssub.with.overflow.i8(i8 [[X:%.*]], i8 100)
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    [[C1:%.*]] = icmp slt i8 [[VAL]], 27
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.ssub.with.overflow(i8 %x, i8 100)
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %val = extractvalue {i8, i1} %val_ov, 0
  %c1 = icmp slt i8 %val, 27
  store i1 %c1, ptr %pc
  %c2 = icmp sle i8 %val, 27
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @umul_val(i8 %x, ptr %pc) {
; CHECK-LABEL: @umul_val(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.umul.with.overflow.i8(i8 [[X:%.*]], i8 10)
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    [[C1:%.*]] = icmp ult i8 [[VAL]], -6
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.umul.with.overflow(i8 %x, i8 10)
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %val = extractvalue {i8, i1} %val_ov, 0
  %c1 = icmp ult i8 %val, 250
  store i1 %c1, ptr %pc
  %c2 = icmp ule i8 %val, 250
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @smul_val_bound1(i8 %x, ptr %pc) {
; CHECK-LABEL: @smul_val_bound1(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.smul.with.overflow.i8(i8 [[X:%.*]], i8 10)
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    [[C1:%.*]] = icmp slt i8 [[VAL]], 120
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.smul.with.overflow(i8 %x, i8 10)
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %val = extractvalue {i8, i1} %val_ov, 0
  %c1 = icmp slt i8 %val, 120
  store i1 %c1, ptr %pc
  %c2 = icmp sle i8 %val, 120
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}

define i1 @smul_val_bound2(i8 %x, ptr %pc) {
; CHECK-LABEL: @smul_val_bound2(
; CHECK-NEXT:    [[VAL_OV:%.*]] = call { i8, i1 } @llvm.smul.with.overflow.i8(i8 [[X:%.*]], i8 10)
; CHECK-NEXT:    [[OV:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 1
; CHECK-NEXT:    br i1 [[OV]], label [[TRAP:%.*]], label [[NO_OVERFLOW:%.*]]
; CHECK:       no_overflow:
; CHECK-NEXT:    [[VAL:%.*]] = extractvalue { i8, i1 } [[VAL_OV]], 0
; CHECK-NEXT:    [[C1:%.*]] = icmp sgt i8 [[VAL]], -120
; CHECK-NEXT:    store i1 [[C1]], ptr [[PC:%.*]], align 1
; CHECK-NEXT:    ret i1 true
; CHECK:       trap:
; CHECK-NEXT:    call void @llvm.trap()
; CHECK-NEXT:    unreachable
;
  %val_ov = call {i8, i1} @llvm.smul.with.overflow(i8 %x, i8 10)
  %ov = extractvalue {i8, i1} %val_ov, 1
  br i1 %ov, label %trap, label %no_overflow

no_overflow:
  %val = extractvalue {i8, i1} %val_ov, 0
  %c1 = icmp sgt i8 %val, -120
  store i1 %c1, ptr %pc
  %c2 = icmp sge i8 %val, -120
  ret i1 %c2

trap:
  call void @llvm.trap()
  unreachable
}
