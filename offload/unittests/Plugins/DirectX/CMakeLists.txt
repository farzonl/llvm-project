add_unittest(OffloadUnitTests directx.unittests
  DirectXTest.cpp ManifestTest.cpp ShaderTest.cpp)
target_compile_definitions(directx.unittests PRIVATE
  DIRECTX_TEST_INPUTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Inputs")
target_link_libraries(directx.unittests PRIVATE LLVMOffload omptarget.rtl.directx)
if(WIN32)
  target_compile_definitions(directx.unittests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
else()
  target_include_directories(directx.unittests SYSTEM PRIVATE
    ${LIBOMPTARGET_DEP_DIRECTX_INCLUDE_DIR}
    ${LIBOMPTARGET_DEP_DIRECTX_INCLUDE_DIR}/wsl/stubs)
endif()
