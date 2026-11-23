; RUN: opt -S -passes=dxil-legalize -verify-each %s | FileCheck %s

target triple = "dxil-pc-shadermodel6.3-library"

; CHECK-LABEL: define i32 @phi(
; CHECK: %p = phi i32 [ 0, %entry ], [ 1, %left ]
; CHECK: ret i32 %p
define i32 @phi(i1 %c) {
entry:
  br i1 %c, label %left, label %end
left:
  br label %end
end:
  %p = phi i3 [ 0, %entry ], [ 1, %left ]
  %r = zext i3 %p to i32
  ret i32 %r
}

; CHECK-LABEL: define i32 @loop(
; CHECK: %value = phi i32 [ 127, %entry ], [ [[BACK:%.*]], %body ]
; CHECK: [[SUM:%.*]] = add i32 %value, 1
; CHECK: [[BACK]] = and i32 [[SUM]], 255
; CHECK: [[SIGN:%.*]] = shl i32 [[SUM]], 24
; CHECK: [[RESULT:%.*]] = ashr i32 [[SIGN]], 24
; CHECK: ret i32 [[RESULT]]
define i32 @loop(i32 %count) {
entry:
  br label %body
body:
  %iteration = phi i32 [ 0, %entry ], [ %next, %body ]
  %value = phi i8 [ 127, %entry ], [ %sum, %body ]
  %sum = add i8 %value, 1
  %next = add i32 %iteration, 1
  %more = icmp ult i32 %next, %count
  br i1 %more, label %body, label %end
end:
  %result = sext i8 %sum to i32
  ret i32 %result
}
