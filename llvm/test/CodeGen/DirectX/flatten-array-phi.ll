; RUN: opt -S -passes=dxil-flatten-arrays -mtriple=dxil-pc-shadermodel6.0-compute -verify-each %s | FileCheck %s
; RUN: opt -S -dxil-flatten-arrays -verify-each %s | FileCheck %s

; Scalar GEPs through loop-carried pointers need no array flattening.
define i32 @scalar_loop(ptr %start, i32 %count) {
; CHECK-LABEL: define i32 @scalar_loop(
; CHECK: %p = phi ptr [ %start, %entry ], [ %next, %loop ]
; CHECK: %next = getelementptr inbounds i32, ptr %p, i32 1
entry:
  br label %loop

loop:
  %p = phi ptr [ %start, %entry ], [ %next, %loop ]
  %remaining = phi i32 [ %count, %entry ], [ %dec, %loop ]
  %value = load i32, ptr %p
  %next = getelementptr inbounds i32, ptr %p, i32 1
  %dec = sub i32 %remaining, 1
  %more = icmp ne i32 %dec, 0
  br i1 %more, label %loop, label %exit

exit:
  ret i32 %value
}

; Advancing a loop-carried array pointer preserves the complete array stride.
define ptr @array_loop(ptr %start, i32 %count) {
; CHECK-LABEL: define ptr @array_loop(
; CHECK: %p = phi ptr [ %start, %entry ], [ [[NEXT:%.*]], %loop ]
; CHECK: [[NEXT]] = getelementptr inbounds [6 x i32], ptr %p, i32 0, i32 6
; CHECK: ret ptr [[NEXT]]
entry:
  br label %loop

loop:
  %p = phi ptr [ %start, %entry ], [ %next, %loop ]
  %remaining = phi i32 [ %count, %entry ], [ %dec, %loop ]
  %next = getelementptr inbounds [2 x [3 x i32]], ptr %p, i32 1
  %dec = sub i32 %remaining, 1
  %more = icmp ne i32 %dec, 0
  br i1 %more, label %loop, label %exit

exit:
  ret ptr %next
}

; The PHI's incoming arrays are flattened, but its source type still describes
; the original row stride. Flatten relative to the selected pointer.
define i32 @select_array(i1 %condition, i32 %row, i32 %column) {
; CHECK-LABEL: define i32 @select_array(
; CHECK: %a.1dim = alloca [6 x i32]
; CHECK: %b.1dim = alloca [6 x i32]
; CHECK: %p = phi ptr [ %a.1dim, %left ], [ %b.1dim, %right ]
; CHECK: [[ROW:%.*]] = mul i32 %row, 3
; CHECK: [[ROWIDX:%.*]] = add i32 0, [[ROW]]
; CHECK: [[COL:%.*]] = mul i32 %column, 1
; CHECK: [[INDEX:%.*]] = add i32 [[ROWIDX]], [[COL]]
; CHECK: [[GEP:%.*]] = getelementptr inbounds [6 x i32], ptr %p, i32 0, i32 [[INDEX]]
; CHECK: %value = load i32, ptr [[GEP]]
entry:
  %a = alloca [2 x [3 x i32]]
  %b = alloca [2 x [3 x i32]]
  br i1 %condition, label %left, label %right

left:
  br label %merge

right:
  br label %merge

merge:
  %p = phi ptr [ %a, %left ], [ %b, %right ]
  %element = getelementptr inbounds [2 x [3 x i32]], ptr %p, i32 0, i32 %row, i32 %column
  %value = load i32, ptr %element
  ret i32 %value
}
