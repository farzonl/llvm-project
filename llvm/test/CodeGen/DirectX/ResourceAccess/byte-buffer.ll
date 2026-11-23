; RUN: opt -S -verify-each -dxil-resource-access %s | FileCheck %s
; RUN: opt -S -verify-each -dxil-resource-access -dxil-op-lower %s | FileCheck %s --check-prefix=DXIL

target triple = "dxil-pc-shadermodel6.0-compute"

; CHECK-LABEL: define i8 @load_byte(
; CHECK: [[ADDRESS:%.*]] = add i32 %base, %offset
; CHECK: [[WORD:%.*]] = and i32 [[ADDRESS]], -4
; CHECK: [[LOW:%.*]] = and i32 [[ADDRESS]], 3
; CHECK: [[SHIFT:%.*]] = shl i32 [[LOW]], 3
; CHECK: [[LOAD:%.*]] = call { i32, i1 } @llvm.dx.resource.load.rawbuffer
; CHECK-SAME: i32 [[WORD]], i32 poison
; CHECK: [[VALUE:%.*]] = extractvalue { i32, i1 } [[LOAD]], 0
; CHECK: [[BYTE:%.*]] = lshr i32 [[VALUE]], [[SHIFT]]
; CHECK: [[RESULT:%.*]] = trunc i32 [[BYTE]] to i8
; CHECK: ret i8 [[RESULT]]
; DXIL-LABEL: define i8 @load_byte(
; DXIL: call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32
define i8 @load_byte(i32 %base, i32 %offset) {
  %handle = call target("dx.RawBuffer", i8, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr @llvm.dx.resource.getpointer(
      target("dx.RawBuffer", i8, 0, 0) %handle, i32 %base)
  %address = getelementptr i8, ptr %ptr, i32 %offset
  %value = load i8, ptr %address
  ret i8 %value
}

; CHECK-LABEL: define void @store_byte(
; CHECK: [[ADDRESS:%.*]] = add i32 %base, %offset
; CHECK: [[WORD:%.*]] = and i32 [[ADDRESS]], -4
; CHECK: [[LOW:%.*]] = and i32 [[ADDRESS]], 3
; CHECK: [[SHIFT:%.*]] = shl i32 [[LOW]], 3
; CHECK: [[BITS:%.*]] = shl i32 255, [[SHIFT]]
; CHECK: [[MASK:%.*]] = xor i32 [[BITS]], -1
; CHECK: [[WIDE:%.*]] = zext i8 %value to i32
; CHECK: [[BYTE:%.*]] = shl i32 [[WIDE]], [[SHIFT]]
; CHECK: br label %byte.store.retry
; CHECK: byte.store.retry:
; CHECK: [[EXPECTED:%.*]] = phi i32 [ 0, %entry ], [ [[ORIGINAL:%.*]], %byte.store.retry ]
; CHECK: [[KEPT:%.*]] = and i32 [[EXPECTED]], [[MASK]]
; CHECK: [[UPDATED:%.*]] = or i32 [[KEPT]], [[BYTE]]
; CHECK: [[ORIGINAL]] = call i32 @llvm.dx.resource.atomic.compare.exchange
; CHECK-SAME: i32 [[WORD]], i32 poison, i32 poison, i32 [[EXPECTED]], i32 [[UPDATED]]
; CHECK: [[SUCCESS:%.*]] = icmp eq i32 [[ORIGINAL]], [[EXPECTED]]
; CHECK: br i1 [[SUCCESS]], label %byte.store.end, label %byte.store.retry, !prof
; CHECK: byte.store.end:
; CHECK-NEXT: ret void
; DXIL-LABEL: define void @store_byte(
; DXIL: call i32 @dx.op.atomicCompareExchange.i32(i32 79,
; DXIL-NOT: @dx.op.bufferStore
define void @store_byte(i32 %base, i32 %offset, i8 %value) !prof !0 {
entry:
  %handle = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr @llvm.dx.resource.getpointer(
      target("dx.RawBuffer", i8, 1, 0) %handle, i32 %base)
  %address = getelementptr i8, ptr %ptr, i32 %offset
  store i8 %value, ptr %address
  ret void
}

!0 = !{!"function_entry_count", i64 1}
