cmake_minimum_required(VERSION 3.20)
project(DirectXSYCL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "This experimental demo requires Windows and the modified clang-cl")
endif()

set(OFFLOAD_RUNTIME_BUILD "" CACHE PATH "Configured offload and libsycl runtime build")
find_package(LLVM REQUIRED CONFIG)
find_path(SYCL_INCLUDE_DIR sycl/sycl.hpp REQUIRED)
find_library(SYCL_LIBRARY NAMES LLVMSYCL
  PATHS "${OFFLOAD_RUNTIME_BUILD}/lib" NO_DEFAULT_PATH REQUIRED)
find_file(SYCL_RUNTIME_DLL LLVMSYCL.dll REQUIRED)
find_program(DEVICE_CLANG NAMES clang HINTS "${LLVM_TOOLS_BINARY_DIR}" REQUIRED)
find_program(DEVICE_OPT NAMES opt HINTS "${LLVM_TOOLS_BINARY_DIR}" REQUIRED)
find_program(DEVICE_LLC NAMES llc HINTS "${LLVM_TOOLS_BINARY_DIR}" REQUIRED)
find_program(OFFLOAD_PACKAGER NAMES llvm-offload-binary
  HINTS "${LLVM_TOOLS_BINARY_DIR}" REQUIRED)
find_program(DXIL_VALIDATOR NAMES dxv REQUIRED)

if(NOT DEFINED ENV{INCLUDE} OR "$ENV{INCLUDE}" STREQUAL "")
  message(FATAL_ERROR "Configure from an x64 Visual Studio developer prompt")
endif()
set(DEVICE_INCLUDES)
foreach(INCLUDE_DIR IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
  list(APPEND DEVICE_INCLUDES -isystem "${INCLUDE_DIR}")
endforeach()
set(HOST_INCLUDES "$ENV{INCLUDE}")
foreach(INCLUDE_DIR IN LISTS HOST_INCLUDES)
  list(APPEND DEVICE_INCLUDES -isystem "${INCLUDE_DIR}")
endforeach()

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/vector-add.bin"
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/vector-add.bc"
    "${CMAKE_CURRENT_BINARY_DIR}/vector-add.kernel.bc"
    "${CMAKE_CURRENT_BINARY_DIR}/vector-add.unsigned.dxil"
    "${CMAKE_CURRENT_BINARY_DIR}/vector-add.dxil"
  COMMAND "${DEVICE_CLANG}" --target=dxil-pc-shadermodel6.0-compute
    -x c++ -std=c++17 -O1 -fno-exceptions -fms-extensions
    "-fms-compatibility-version=${CMAKE_CXX_SIMULATE_VERSION}"
    -D_WIN32 -D_WIN64 -D_MT -D_DLL -D_CRT_USE_BUILTIN_OFFSETOF
    -Xclang -fsycl-is-device -Xclang -aux-triple
    -Xclang x86_64-pc-windows-msvc
    "-I${SYCL_INCLUDE_DIR}" ${DEVICE_INCLUDES}
    -emit-llvm -c "${CMAKE_CURRENT_SOURCE_DIR}/vector-add.cpp"
    -o "${CMAKE_CURRENT_BINARY_DIR}/vector-add.bc"
  COMMAND "${DEVICE_OPT}" -dxil-kernel-abi
    "${CMAKE_CURRENT_BINARY_DIR}/vector-add.bc"
    -o "${CMAKE_CURRENT_BINARY_DIR}/vector-add.kernel.bc"
  COMMAND "${DEVICE_LLC}" -filetype=obj
    "${CMAKE_CURRENT_BINARY_DIR}/vector-add.kernel.bc"
    -o "${CMAKE_CURRENT_BINARY_DIR}/vector-add.unsigned.dxil"
  COMMAND "${DXIL_VALIDATOR}" "-o=${CMAKE_CURRENT_BINARY_DIR}/vector-add.dxil"
    "${CMAKE_CURRENT_BINARY_DIR}/vector-add.unsigned.dxil"
  COMMAND "${OFFLOAD_PACKAGER}" -o "${CMAKE_CURRENT_BINARY_DIR}/vector-add.bin"
    "--image=file=${CMAKE_CURRENT_BINARY_DIR}/vector-add.dxil,kind=sycl,triple=dxil-pc-shadermodel6.0-compute,directx-metadata=${CMAKE_CURRENT_BINARY_DIR}/vector-add.kernel.bc"
  DEPENDS vector-add.cpp "${DEVICE_CLANG}" "${DEVICE_OPT}" "${DEVICE_LLC}"
    "${OFFLOAD_PACKAGER}" "${DXIL_VALIDATOR}"
  VERBATIM)
add_custom_target(sycl-device-image
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/vector-add.bin")

add_executable(directx-sycl-vector-add vector-add.cpp)
add_dependencies(directx-sycl-vector-add sycl-device-image)
target_include_directories(directx-sycl-vector-add PRIVATE "${SYCL_INCLUDE_DIR}")
target_link_libraries(directx-sycl-vector-add PRIVATE "${SYCL_LIBRARY}")
set_property(TARGET directx-sycl-vector-add PROPERTY
  MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_compile_options(directx-sycl-vector-add PRIVATE /EHsc
  "SHELL:/clang:-Xclang /clang:-fsycl-is-host"
  "SHELL:/clang:-Xclang /clang:-foffload-include-binary"
  "SHELL:/clang:-Xclang \"/clang:${CMAKE_CURRENT_BINARY_DIR}/vector-add.bin\"")
set_property(SOURCE vector-add.cpp APPEND PROPERTY OBJECT_DEPENDS
  "${CMAKE_CURRENT_BINARY_DIR}/vector-add.bin")
add_custom_command(TARGET directx-sycl-vector-add POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${SYCL_RUNTIME_DLL}" "${OFFLOAD_RUNTIME_BUILD}/lib/LLVMOffload.dll"
    "$<TARGET_FILE_DIR:directx-sycl-vector-add>"
  VERBATIM)
