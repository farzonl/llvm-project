# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

add_library(SYCL::SYCL INTERFACE IMPORTED GLOBAL)
target_link_libraries(SYCL::SYCL INTERFACE LibSYCL::Runtime)

function(libsycl_cts_write_manifest)
  get_property(Targets GLOBAL PROPERTY LIBSYCL_CTS_TARGETS)
  list(SORT Targets)
  set(Entries "")
  foreach(Target IN LISTS Targets)
    get_target_property(ObjectTarget ${Target} LIBSYCL_CTS_OBJECT_TARGET)
    list(APPEND Entries
      "{\"target\":\"${Target}\",\"object_target\":\"${ObjectTarget}\",\"executable\":\"$<TARGET_FILE:${Target}>\"}")
  endforeach()
  list(JOIN Entries ",\n" Entries)
  foreach(Option FULL_CONFORMANCE DEPRECATED_FEATURES_TESTS OPENCL_INTEROP_TESTS)
    if(SYCL_CTS_ENABLE_${Option})
      set(${Option} true)
    else()
      set(${Option} false)
    endif()
  endforeach()
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/libsycl-cts-targets.json" CONTENT
    "{\n\"source_dir\":\"${CMAKE_SOURCE_DIR}\",\n\"compiler\":\"${CMAKE_CXX_COMPILER}\",\n\"full_conformance\":${FULL_CONFORMANCE},\n\"deprecated_features\":${DEPRECATED_FEATURES_TESTS},\n\"opencl_interop\":${OPENCL_INTEROP_TESTS},\n\"tests\":[${Entries}]\n}\n")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
  CALL libsycl_cts_write_manifest)

function(add_sycl_executable_implementation)
  cmake_parse_arguments(args "" "NAME;OBJECT_LIBRARY" "TESTS" ${ARGN})
  if(NOT args_NAME OR NOT args_OBJECT_LIBRARY OR NOT args_TESTS OR
     args_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "CTS executables require NAME, OBJECT_LIBRARY, and TESTS")
  endif()
  add_library(${args_OBJECT_LIBRARY} OBJECT ${args_TESTS})
  add_executable(${args_NAME} $<TARGET_OBJECTS:${args_OBJECT_LIBRARY}>)
  set_target_properties(${args_OBJECT_LIBRARY} PROPERTIES
    INCLUDE_DIRECTORIES $<TARGET_PROPERTY:${args_NAME},INCLUDE_DIRECTORIES>
    COMPILE_DEFINITIONS $<TARGET_PROPERTY:${args_NAME},COMPILE_DEFINITIONS>
    COMPILE_OPTIONS $<TARGET_PROPERTY:${args_NAME},COMPILE_OPTIONS>
    COMPILE_FEATURES $<TARGET_PROPERTY:${args_NAME},COMPILE_FEATURES>)
  add_sycl_to_target(TARGET ${args_OBJECT_LIBRARY} SOURCES ${args_TESTS})
  set_property(TARGET ${args_NAME} PROPERTY
    LIBSYCL_CTS_OBJECT_TARGET ${args_OBJECT_LIBRARY})
  set_property(GLOBAL APPEND PROPERTY LIBSYCL_CTS_TARGETS ${args_NAME})

  add_custom_command(TARGET ${args_NAME} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${SYCL_RUNTIME_DLL}" "${OFFLOAD_RUNTIME_DLL}"
      "$<TARGET_FILE_DIR:${args_NAME}>"
    VERBATIM)
endfunction()
