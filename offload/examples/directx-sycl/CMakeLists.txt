cmake_minimum_required(VERSION 3.20)
project(DirectXSYCL LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "This experimental demo requires Windows and the modified clang-cl")
endif()

set(OFFLOAD_RUNTIME_BUILD "" CACHE PATH "Configured offload and libsycl runtime build")
find_path(SYCL_INCLUDE_DIR sycl/sycl.hpp REQUIRED)
find_library(SYCL_LIBRARY NAMES LLVMSYCL
  PATHS "${OFFLOAD_RUNTIME_BUILD}/lib" NO_DEFAULT_PATH REQUIRED)
find_file(SYCL_RUNTIME_DLL LLVMSYCL.dll REQUIRED)
find_program(DXIL_VALIDATOR NAMES dxv REQUIRED)

# CMake's MSVC rules invoke the native linker directly. Offloading must instead
# pass through the compiler driver so that device images are linked and wrapped.
set(CMAKE_CXX_LINK_EXECUTABLE
  "<CMAKE_CXX_COMPILER> /nologo /MD /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute /clang:-Xoffload-linker \"/clang:--dxil-validator=${DXIL_VALIDATOR}\" <OBJECTS> /Fe<TARGET> /link <LINK_FLAGS> <LINK_LIBRARIES>")

add_custom_target(sycl-runtime-dlls
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "$<TARGET_FILE_DIR:directx-sycl-vector-add>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${SYCL_RUNTIME_DLL}" "${OFFLOAD_RUNTIME_BUILD}/lib/LLVMOffload.dll"
    "$<TARGET_FILE_DIR:directx-sycl-vector-add>"
  VERBATIM)

function(add_directx_sycl_executable TARGET SOURCE)
  add_executable(${TARGET} "${SOURCE}")
  add_dependencies(${TARGET} sycl-runtime-dlls)
  target_include_directories(${TARGET} PRIVATE "${SYCL_INCLUDE_DIR}")
  target_link_libraries(${TARGET} PRIVATE "${SYCL_LIBRARY}")
  target_compile_definitions(${TARGET} PRIVATE ${ARGN})
  set_property(TARGET ${TARGET} PROPERTY
    MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  target_compile_options(${TARGET} PRIVATE /EHsc
    /clang:-fsycl /clang:--offload-targets=dxil-pc-shadermodel6.0-compute)
endfunction()

add_directx_sycl_executable(directx-sycl-vector-add
  "${CMAKE_CURRENT_SOURCE_DIR}/vector-add.cpp")

option(DIRECTX_SYCL_BUILD_TESTS "Build real SYCL geometry and multi-kernel probes" OFF)
if(DIRECTX_SYCL_BUILD_TESTS)
  enable_testing()
  foreach(DIMS RANGE 1 3)
    add_directx_sycl_executable(directx-sycl-ndrange-${DIMS}
      "${CMAKE_CURRENT_SOURCE_DIR}/../../unittests/Plugins/DirectX/Inputs/sycl-ndrange.cpp"
      TEST_DIMS=${DIMS})
    add_test(NAME directx-sycl-ndrange-${DIMS}
      COMMAND directx-sycl-ndrange-${DIMS})
  endforeach()
  add_directx_sycl_executable(directx-sycl-multi-kernel
    "${CMAKE_CURRENT_SOURCE_DIR}/../../unittests/Plugins/DirectX/Inputs/sycl-multi-kernel.cpp")
  target_sources(directx-sycl-multi-kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../unittests/Plugins/DirectX/Inputs/sycl-multi-kernel-host.cpp")
  add_test(NAME directx-sycl-multi-kernel COMMAND directx-sycl-multi-kernel)
endif()
