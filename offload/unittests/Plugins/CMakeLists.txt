add_unittest(OffloadUnitTests plugin-common.unittests
  GlobalHandlerTest.cpp
  PluginContextTest.cpp)
target_link_libraries(plugin-common.unittests PRIVATE omptarget.rtl.host)
target_include_directories(plugin-common.unittests PRIVATE
  ${LIBOMPTARGET_INCLUDE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/../../liboffload/API)
target_compile_definitions(plugin-common.unittests PRIVATE
  TARGET_NAME=PluginCommon
  DEBUG_PREFIX="PluginCommon")

if(TARGET omptarget.rtl.directx)
  add_subdirectory(DirectX)
endif()
