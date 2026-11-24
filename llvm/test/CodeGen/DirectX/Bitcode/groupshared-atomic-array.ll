; RUN: llc --filetype=obj %s --stop-after=dxil-writer -o %t.bc
; RUN: llvm-dis %t.bc -o - | FileCheck %s

target triple = "dxil-pc-shadermodel6.0-compute"
@words = internal addrspace(3) global [2 x i32] poison, align 4

; CHECK-LABEL: define void @main()
; CHECK: [[FIRST:%.*]] = bitcast ptr addrspace(3) @words to ptr addrspace(3)
; CHECK: atomicrmw or ptr addrspace(3) [[FIRST]], i32 0 seq_cst
; CHECK: [[SECOND:%.*]] = bitcast ptr addrspace(3) @words to ptr addrspace(3)
; CHECK: cmpxchg ptr addrspace(3) [[SECOND]], i32 0, i32 1 seq_cst seq_cst
define void @main() #0 {
  %old = atomicrmw or ptr addrspace(3) @words, i32 0 seq_cst
  %exchange = cmpxchg ptr addrspace(3) @words, i32 0, i32 1 seq_cst seq_cst
  ret void
}
attributes #0 = { "hlsl.numthreads"="1,1,1" "hlsl.shader"="compute" }
