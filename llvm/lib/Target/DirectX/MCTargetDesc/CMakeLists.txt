add_llvm_component_library(LLVMDirectXDesc
  DirectXContainerObjectWriter.cpp
  DirectXInstPrinter.cpp
  DirectXMCTargetDesc.cpp
  DirectXMCCodeEmitter.cpp
  

  LINK_COMPONENTS
  DirectXInfo
  MC
  Support
  TargetParser

  ADD_TO_COMPONENT
  DirectX
  )
