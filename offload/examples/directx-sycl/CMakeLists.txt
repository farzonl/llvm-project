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

add_custom_target(sycl-runtime-dlls
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "$<TARGET_FILE_DIR:directx-sycl-vector-add>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${SYCL_RUNTIME_DLL}" "${OFFLOAD_RUNTIME_BUILD}/lib/LLVMOffload.dll"
    "$<TARGET_FILE_DIR:directx-sycl-vector-add>"
  VERBATIM)

function(add_directx_sycl_executable TARGET SOURCE)
  set(IMAGE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bin")
  set(DEVICE_DEFINITIONS)
  foreach(DEFINITION IN LISTS ARGN)
    list(APPEND DEVICE_DEFINITIONS "-D${DEFINITION}")
  endforeach()
  add_custom_command(
    OUTPUT "${IMAGE}"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bc"
      "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.kernel.bc"
      "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.unsigned.dxil"
      "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dxil"
      "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.d"
    COMMAND "${DEVICE_CLANG}" --target=dxil-pc-shadermodel6.0-compute
      -x c++ -std=c++17 -O2 -fno-exceptions -fms-extensions
      "-fms-compatibility-version=${CMAKE_CXX_SIMULATE_VERSION}"
      -D_WIN32 -D_WIN64 -D_MT -D_DLL -D_CRT_USE_BUILTIN_OFFSETOF
      -Xclang -fsycl-is-device -Xclang -aux-triple
      -Xclang x86_64-pc-windows-msvc
      "-I${SYCL_INCLUDE_DIR}" ${DEVICE_INCLUDES} ${DEVICE_DEFINITIONS}
      -MD -MF "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.d" -MT "${IMAGE}"
      -emit-llvm -c "${SOURCE}"
      -o "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bc"
    COMMAND "${DEVICE_OPT}" -dxil-kernel-abi
      "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.bc"
      -o "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.kernel.bc"
    COMMAND "${DEVICE_LLC}" -filetype=obj
      "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.kernel.bc"
      -o "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.unsigned.dxil"
    COMMAND "${DXIL_VALIDATOR}" "-o=${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dxil"
      "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.unsigned.dxil"
    COMMAND "${OFFLOAD_PACKAGER}" -o "${IMAGE}"
      "--image=file=${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.dxil,kind=sycl,triple=dxil-pc-shadermodel6.0-compute,directx-metadata=${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.kernel.bc"
    DEPENDS "${SOURCE}" "${DEVICE_CLANG}" "${DEVICE_OPT}" "${DEVICE_LLC}"
      "${OFFLOAD_PACKAGER}" "${DXIL_VALIDATOR}"
    DEPFILE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.d"
    VERBATIM)
  add_custom_target(${TARGET}-device-image DEPENDS "${IMAGE}")

  add_executable(${TARGET} "${SOURCE}")
  add_dependencies(${TARGET} ${TARGET}-device-image sycl-runtime-dlls)
  target_include_directories(${TARGET} PRIVATE "${SYCL_INCLUDE_DIR}")
  target_link_libraries(${TARGET} PRIVATE "${SYCL_LIBRARY}")
  target_compile_definitions(${TARGET} PRIVATE ${ARGN})
  set_property(TARGET ${TARGET} PROPERTY
    MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  target_compile_options(${TARGET} PRIVATE /EHsc
    "SHELL:/clang:-Xclang /clang:-fsycl-is-host"
    "SHELL:/clang:-Xclang /clang:-foffload-include-binary"
    "SHELL:/clang:-Xclang \"/clang:${IMAGE}\"")
endfunction()

add_directx_sycl_executable(directx-sycl-vector-add
  "${CMAKE_CURRENT_SOURCE_DIR}/vector-add.cpp")
add_custom_target(sycl-device-image)
add_dependencies(sycl-device-image directx-sycl-vector-add-device-image)

option(DIRECTX_SYCL_BUILD_TESTS "Build real 1D/2D/3D SYCL geometry probes" OFF)
if(DIRECTX_SYCL_BUILD_TESTS)
  enable_testing()
  foreach(DIMS RANGE 1 3)
    add_directx_sycl_executable(directx-sycl-ndrange-${DIMS}
      "${CMAKE_CURRENT_SOURCE_DIR}/../../unittests/Plugins/DirectX/Inputs/sycl-ndrange.cpp"
      TEST_DIMS=${DIMS})
    add_test(NAME directx-sycl-ndrange-${DIMS}
      COMMAND directx-sycl-ndrange-${DIMS})
  endforeach()
endif()
