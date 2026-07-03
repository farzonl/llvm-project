; We use llc for this test so that we don't abort after the first error.
; RUN: not llc %s -o /dev/null 2>&1 | FileCheck %s

target triple = "dxil-pc-shadermodel6.3-library"

; DXIL operation USubb only supports i32. Other integer types are unsupported.
; CHECK: error:
; CHECK-SAME: in function usubb_i16
; CHECK-SAME: Cannot create USubb operation: Invalid overload type

define noundef i16 @usubb_i16(i16 noundef %a, i16 noundef %b) "hlsl.export" {
  %usubb = call { i16, i1 } @llvm.usub.with.overflow.i16(i16 %a, i16 %b)
  %borrow = extractvalue { i16, i1 } %usubb, 1
  %diff = extractvalue { i16, i1 } %usubb, 0
  %borrow_zext = zext i1 %borrow to i16
  %result = sub i16 %diff, %borrow_zext
  ret i16 %result
}

; CHECK: error:
; CHECK-SAME: in function usubb_return
; CHECK-SAME: DXIL ops that return structs may only be used by insert- and extractvalue

define noundef { i32, i1 } @usubb_return(i32 noundef %a, i32 noundef %b) "hlsl.export" {
  %usubb = call { i32, i1 } @llvm.usub.with.overflow.i32(i32 %a, i32 %b)
  ret { i32, i1 } %usubb
}

declare { i16, i1 } @llvm.usub.with.overflow.i16(i16, i16)
