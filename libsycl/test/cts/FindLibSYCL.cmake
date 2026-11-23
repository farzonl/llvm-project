# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(CMAKE_VERSION VERSION_LESS 3.20)
  message(FATAL_ERROR "The libsycl CTS adapter requires CMake 3.20 or newer")
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "The DirectX CTS adapter requires Windows and the modified clang-cl")
endif()
if(CMAKE_MSVC_RUNTIME_LIBRARY AND
   NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreadedDLL")
  message(FATAL_ERROR "The DirectX CTS adapter requires MultiThreadedDLL")
endif()
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  message(FATAL_ERROR "The DirectX CTS adapter requires a Release build with the DLL CRT")
endif()

set(OFFLOAD_RUNTIME_BUILD "" CACHE PATH "Configured offload and libsycl runtime build")
find_path(SYCL_INCLUDE_DIR sycl/sycl.hpp REQUIRED)
find_library(SYCL_LIBRARY NAMES LLVMSYCL
  PATHS "${OFFLOAD_RUNTIME_BUILD}/lib" NO_DEFAULT_PATH REQUIRED)
find_file(SYCL_RUNTIME_DLL LLVMSYCL.dll REQUIRED)
find_file(OFFLOAD_RUNTIME_DLL LLVMOffload.dll
  PATHS "${OFFLOAD_RUNTIME_BUILD}/lib" NO_DEFAULT_PATH REQUIRED)
find_program(DXIL_VALIDATOR NAMES dxv REQUIRED)

set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_CXX_STANDARD 17)

add_library(LibSYCL::Runtime INTERFACE IMPORTED GLOBAL)
set_target_properties(LibSYCL::Runtime PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${SYCL_INCLUDE_DIR}"
  INTERFACE_LINK_LIBRARIES "${SYCL_LIBRARY}")

# Keep host-only CTS utilities out of device compilation. The compiler driver
# still handles final device linking for executables containing offload objects.
set(CMAKE_CXX_LINK_EXECUTABLE
  "<CMAKE_CXX_COMPILER> /nologo /MD /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute /clang:-Xoffload-linker \"/clang:--dxil-validator=${DXIL_VALIDATOR}\" <OBJECTS> /Fe<TARGET> /link <LINK_FLAGS> <LINK_LIBRARIES>")

function(add_sycl_to_target)
  cmake_parse_arguments(ADD_SYCL "" "TARGET" "SOURCES" ${ARGN})
  if(NOT ADD_SYCL_TARGET OR ADD_SYCL_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "add_sycl_to_target requires TARGET and optional SOURCES")
  endif()
  target_link_libraries(${ADD_SYCL_TARGET} PUBLIC SYCL::SYCL)
  target_compile_options(${ADD_SYCL_TARGET} PRIVATE
    /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute)
endfunction()
