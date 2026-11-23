cmake_minimum_required(VERSION 3.20)
project(LibSYCLCTSAdapterSmoke LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../FindLibSYCL.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../AdaptLibSYCL.cmake")

get_filename_component(REPOSITORY_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(INPUTS "${REPOSITORY_ROOT}/offload/unittests/Plugins/DirectX/Inputs")
add_sycl_executable_implementation(
  NAME test_cts_adapter
  OBJECT_LIBRARY test_cts_adapter_objects
  TESTS "${INPUTS}/sycl-multi-kernel.cpp" "${INPUTS}/sycl-multi-kernel-host.cpp")
target_link_libraries(test_cts_adapter PRIVATE SYCL::SYCL)

enable_testing()
add_test(NAME test_cts_adapter COMMAND test_cts_adapter 0)
