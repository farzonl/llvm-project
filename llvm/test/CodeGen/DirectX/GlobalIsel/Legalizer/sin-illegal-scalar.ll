; RUN: not --crash llc --enable-directx-global-isel -O0 --mtriple=dxil-pc-shadermodel6.3-library -stop-after=legalizer -global-isel -verify-machineinstrs %s -o - 2>&1 | FileCheck %s

; CHECK: LLVM ERROR: unable to legalize instruction: %1:id(s64) = G_FSIN %0:id (in function: test_sin_f64)

declare double @llvm.sin.f64(double)
define double @test_sin_f64(double %x) #0 {
  %y = call double @llvm.sin.64(double %x)
  ret double %y
}

attributes #0 = { convergent norecurse nounwind "hlsl.export"}