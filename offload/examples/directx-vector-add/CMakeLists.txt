cmake_minimum_required(VERSION 3.20)
project(DirectXVectorAdd LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OFFLOAD_RUNTIME_BUILD "" CACHE PATH "Configured offload runtime build directory")
find_package(LLVM REQUIRED CONFIG)
find_path(OFFLOAD_INCLUDE_DIR OffloadAPI.h
  PATHS "${OFFLOAD_RUNTIME_BUILD}/offload/liboffload/API"
  NO_DEFAULT_PATH REQUIRED)
find_library(OFFLOAD_LIBRARY NAMES LLVMOffload
  PATHS "${OFFLOAD_RUNTIME_BUILD}/lib" NO_DEFAULT_PATH REQUIRED)
find_program(HLSL_CLANG NAMES clang HINTS "${LLVM_TOOLS_BINARY_DIR}" REQUIRED)
find_program(DXIL_VALIDATOR NAMES dxv REQUIRED)

add_executable(directx-package Package.cpp)
target_include_directories(directx-package SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(directx-package PRIVATE ${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs Object Support)
target_link_libraries(directx-package PRIVATE ${llvm_libs})

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/vector-add.bin"
  COMMAND "${HLSL_CLANG}" --target=dxil-pc-shadermodel6.0-compute
    -x hlsl -O3 -c "${CMAKE_CURRENT_SOURCE_DIR}/vector-add.hlsl"
    -o "${CMAKE_CURRENT_BINARY_DIR}/vector-add.unsigned.dxil"
  COMMAND "${DXIL_VALIDATOR}"
    "-o=${CMAKE_CURRENT_BINARY_DIR}/vector-add.dxil"
    "${CMAKE_CURRENT_BINARY_DIR}/vector-add.unsigned.dxil"
  COMMAND directx-package "${CMAKE_CURRENT_BINARY_DIR}/vector-add.dxil"
    "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json"
    "${CMAKE_CURRENT_BINARY_DIR}/vector-add.bin"
  DEPENDS directx-package vector-add.hlsl manifest.json
  VERBATIM)
add_custom_target(vector-add-shader ALL
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/vector-add.bin")

add_executable(directx-vector-add VectorAdd.cpp)
target_include_directories(directx-vector-add PRIVATE "${OFFLOAD_INCLUDE_DIR}")
target_link_libraries(directx-vector-add PRIVATE "${OFFLOAD_LIBRARY}")
add_dependencies(directx-vector-add vector-add-shader)
if(MSVC)
  target_compile_options(directx-vector-add PRIVATE /EHsc)
endif()
if(WIN32)
  add_custom_command(TARGET directx-vector-add POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${OFFLOAD_RUNTIME_BUILD}/lib/LLVMOffload.dll"
      "$<TARGET_FILE_DIR:directx-vector-add>"
    VERBATIM)
endif()
