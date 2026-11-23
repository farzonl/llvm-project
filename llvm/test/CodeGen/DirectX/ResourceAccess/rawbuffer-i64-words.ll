; RUN: opt -S -dxil-resource-access %s | FileCheck %s

target triple = "dxil-pc-shadermodel6.0-compute"

define i64 @scalar(i64 %value, i32 %offset) {
  %buffer = call target("dx.RawBuffer", i8, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr @llvm.dx.resource.getpointer(
      target("dx.RawBuffer", i8, 1, 0, 0) %buffer, i32 %offset)
  store i64 %value, ptr %ptr
  %result = load i64, ptr %ptr
  ret i64 %result
}

; CHECK-LABEL: define i64 @scalar(
; CHECK: trunc i64 %value to i32
; CHECK: lshr i64 %value, 32
; CHECK: %[[WORDS:.*]] = insertelement <2 x i32> {{.*}}, i32 {{.*}}, i64 1
; CHECK: call void @llvm.dx.resource.store.rawbuffer.{{.*}}.v2i32({{.*}}i32 %offset, i32 poison, <2 x i32> %[[WORDS]])
; CHECK: %[[LOADED:.*]] = call { <2 x i32>, i1 } @llvm.dx.resource.load.rawbuffer.v2i32.{{.*}}({{.*}}i32 %offset, i32 poison)
; CHECK: %[[DATA:.*]] = extractvalue { <2 x i32>, i1 } %[[LOADED]], 0
; CHECK: zext i32 {{.*}} to i64
; CHECK: shl i64 {{.*}}, 32
; CHECK: %[[RESULT:.*]] = or i64 {{.*}}
; CHECK: ret i64 %[[RESULT]]

define <3 x i64> @wide(<3 x i64> %value) {
  %buffer = call target("dx.RawBuffer", i8, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr @llvm.dx.resource.getpointer(
      target("dx.RawBuffer", i8, 1, 0, 0) %buffer, i32 8)
  store <3 x i64> %value, ptr %ptr, align 8
  %result = load <3 x i64>, ptr %ptr, align 8
  ret <3 x i64> %result
}

; CHECK-LABEL: define <3 x i64> @wide(
; CHECK: extractelement <3 x i64> %value, i64 0
; CHECK: call void @llvm.dx.resource.store.rawbuffer.{{.*}}.v4i32({{.*}}i32 8, i32 poison, <4 x i32>
; CHECK: call void @llvm.dx.resource.store.rawbuffer.{{.*}}.v2i32({{.*}}i32 24, i32 poison, <2 x i32>
; CHECK: call { <4 x i32>, i1 } @llvm.dx.resource.load.rawbuffer.v4i32.{{.*}}({{.*}}i32 8, i32 poison)
; CHECK: call { <2 x i32>, i1 } @llvm.dx.resource.load.rawbuffer.v2i32.{{.*}}({{.*}}i32 24, i32 poison)
; CHECK: insertelement <3 x i64> {{.*}}, i64 {{.*}}, i64 2

define void @constant() {
  %buffer = call target("dx.RawBuffer", i8, 1, 0, 0)
      @llvm.dx.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr @llvm.dx.resource.getpointer(
      target("dx.RawBuffer", i8, 1, 0, 0) %buffer, i32 0)
  store i64 1311768467463790320, ptr %ptr
  ret void
}

; CHECK-LABEL: define void @constant(
; CHECK: call void @llvm.dx.resource.store.rawbuffer.{{.*}}.v2i32({{.*}}<2 x i32> <i32 -1698898192, i32 305419896>)
