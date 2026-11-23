; RUN: opt -S -verify-each -dxil-kernel-abi %s | FileCheck %s --check-prefix=ABI
; RUN: opt -dxil-kernel-abi %s -o %t.bc
; RUN: llc -filetype=asm %t.bc -o - | FileCheck %s --check-prefix=DXIL

target triple = "dxil-pc-shadermodel6.0-compute"

; ABI-LABEL: define void @copy_bytes()
; ABI: @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_0_0t
; ABI: @llvm.dx.resource.handlefrombinding.tdx.RawBuffer_i8_1_0t
; ABI: load i8
; ABI: store i8
; ABI: !llvm.offload.manifest
; ABI: \22min_size\22:4
; DXIL: define void @copy_bytes()
; DXIL: call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32
; DXIL: call i32 @dx.op.atomicCompareExchange.i32
; DXIL-NOT: @dx.op.bufferStore
define void @copy_bytes(ptr "dx.readonly" %input, ptr %output) #0 {
  %group = call i32 @llvm.dx.group.id(i32 0)
  %thread = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %base = mul i32 %group, 64
  %index = add i32 %base, %thread
  %source = getelementptr i8, ptr %input, i32 %index
  %value = load i8, ptr %source
  %destination = getelementptr i8, ptr %output, i32 %index
  store i8 %value, ptr %destination
  ret void
}

attributes #0 = { "dx.kernel" "hlsl.numthreads"="64,1,1" }
