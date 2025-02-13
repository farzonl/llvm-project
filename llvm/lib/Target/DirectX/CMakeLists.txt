add_llvm_component_group(DirectX)

set(LLVM_TARGET_DEFINITIONS DirectX.td)

tablegen(LLVM DirectXGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM DirectXGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM DirectXGenGlobalISel.inc -gen-global-isel)
tablegen(LLVM DirectXMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM DirectXGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM DirectXGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM DirectXGenRegisterBank.inc -gen-register-bank)

set(LLVM_TARGET_DEFINITIONS DXIL.td)
tablegen(LLVM DXILOperation.inc -gen-dxil-operation)

add_public_tablegen_target(DirectXCommonTableGen)

add_llvm_target(DirectXCodeGen
  CBufferDataLayout.cpp
  DirectXAsmPrinter.cpp
  DirectXCallLowering.cpp
  DirectXInstrInfo.cpp
  DirectXInstructionSelector.cpp
  DirectXLegalizerInfo.cpp
  DirectXMCInstLower.cpp
  DirectXRegisterBanks.cpp
  DirectXRegisterInfo.cpp
  DirectXSubtarget.cpp
  DirectXTargetMachine.cpp
  DirectXTargetTransformInfo.cpp
  DirectXTypeMap.cpp
  DXContainerGlobals.cpp
  DXILDataScalarization.cpp
  DXILFinalizeLinkage.cpp
  DXILFlattenArrays.cpp
  DXILIntrinsicExpansion.cpp
  DXILOpBuilder.cpp
  DXILOpLowering.cpp
  DXILPrepare.cpp
  DXILPrettyPrinter.cpp
  DXILResource.cpp
  DXILResourceAnalysis.cpp
  DXILResourceAccess.cpp
  DXILShaderFlags.cpp
  DXILTranslateMetadata.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  DXILBitWriter
  DirectXDesc
  DirectXInfo
  DirectXPointerTypeAnalysis
  FrontendHLSL
  MC
  ScalarOpts
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  DirectX
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(DirectXIRPasses)
add_subdirectory(DXILWriter)
