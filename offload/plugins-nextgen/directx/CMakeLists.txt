if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Windows|Linux)$")
  message(FATAL_ERROR "The experimental DirectX plugin requires Windows or Linux")
endif()

add_target_library(omptarget.rtl.directx DirectX)
target_sources(omptarget.rtl.directx PRIVATE src/rtl.cpp)

if(WIN32)
  target_compile_definitions(omptarget.rtl.directx PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX)
else()
  find_path(LIBOMPTARGET_DEP_DIRECTX_INCLUDE_DIR
    NAMES directx/d3d12.h
    DOC "DirectX-Headers include directory"
    REQUIRED)
  target_include_directories(omptarget.rtl.directx SYSTEM PRIVATE
    ${LIBOMPTARGET_DEP_DIRECTX_INCLUDE_DIR}
    ${LIBOMPTARGET_DEP_DIRECTX_INCLUDE_DIR}/wsl/stubs)
endif()
